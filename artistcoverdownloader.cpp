/*
 * Album Mosaic Plugin
 * Copyright 2026, dewiweb
 *
 * Album Mosaic Plugin is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Album Mosaic Plugin is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Album Mosaic Plugin.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "artistcoverdownloader.h"

#include <core/network/networkaccessmanager.h>
#include <core/track.h>
#include <gui/coverrepository.h>
#include <utils/settings/settingsmanager.h>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>

using namespace Qt::StringLiterals;

ArtistCoverDownloader::ArtistCoverDownloader(Fooyin::CorePluginContext* coreContext,
                                             Fooyin::GuiPluginContext* guiContext,
                                             QObject* parent)
    : QObject{parent}
    , m_coreContext{coreContext}
    , m_guiContext{guiContext}
    , m_apiKey{QString::fromLatin1(QByteArray::fromBase64(AccessKeyB64))}
    , m_secret{QString::fromLatin1(QByteArray::fromBase64(SecretB64))}
    , m_throttleTimer{new QTimer(this)}
{
    m_throttleTimer->setSingleShot(true);
    m_throttleTimer->setInterval(1100); // Discogs: 1 req/sec, add 100ms buffer
}

ArtistCoverDownloader::~ArtistCoverDownloader()
{
    cancel();
}

void ArtistCoverDownloader::downloadMissing(const QList<QPair<QString, Fooyin::Track>>& artists)
{
    // Guard against multiple instances starting a download simultaneously
    if(m_running) {
        qDebug() << "[ArtistCoverDownloader] Download already in progress, skipping";
        return;
    }
    // Filter out artists that already have a cover in the plugin cache or
    // in the artist's parent directory — avoids re-downloading on every restart.
    const QString cacheDir = QDir::homePath() + "/.local/share/fooyin/artistcovers";
    QList<QPair<QString, Fooyin::Track>> toDownload;

    for(const auto& [artist, track] : artists) {
        const QString md5 = QString(QCryptographicHash::hash(artist.toUtf8(), QCryptographicHash::Md5).toHex());
        const QString cachePath = cacheDir + "/" + md5 + ".jpg";
        if(QFile::exists(cachePath)) {
            continue;
        }

        if(track.isValid()) {
            const QString trackDir = QFileInfo(track.filepath()).absolutePath();
            const QString artistDir = QDir(trackDir).absoluteFilePath("..");
            // Check both track dir (fooyin native) and artist parent dir
            const QStringList checks = {
                QDir(trackDir).absoluteFilePath("artist.jpg"),
                QDir(trackDir).absoluteFilePath("artist.png"),
                QDir(artistDir).absoluteFilePath("artist.jpg"),
                QDir(artistDir).absoluteFilePath("artist.png"),
            };
            bool found = false;
            for(const QString& path : checks) {
                if(QFile::exists(path)) {
                    found = true;
                    break;
                }
            }
            if(found) continue;
        }

        toDownload.append({artist, track});
    }

    m_queue = toDownload;
    m_totalCount = toDownload.size();
    m_doneCount = 0;
    m_cancelled = false;
    m_running = true;
    qDebug() << "[ArtistCoverDownloader] Starting download for" << m_totalCount << "artists"
             << "(" << artists.size() - m_totalCount << "already have covers)";
    processNext();
}

void ArtistCoverDownloader::downloadSingleArtist(const QString& artistName, const Fooyin::Track& track)
{
    m_cancelled = false;
    m_totalCount = 1;
    m_doneCount = 0;
    m_running = true;
    m_currentArtist = artistName;
    m_currentTrack = track;
    searchArtist();
}

void ArtistCoverDownloader::cancel()
{
    m_cancelled = true;
    m_running = false;
    if(m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_queue.clear();
}

void ArtistCoverDownloader::processNext()
{
    if(m_cancelled) {
        return;
    }

    if(m_queue.isEmpty()) {
        m_running = false;
        qDebug() << "[ArtistCoverDownloader] Finished." << m_doneCount << "/" << m_totalCount << "covers downloaded";
        emit finished();
        return;
    }

    auto item = m_queue.takeFirst();
    m_currentArtist = item.first;
    m_currentTrack = item.second;
    m_triedDeezer = false;

    // Try Deezer first (fast, single request, no auth, no rate limit)
    // Fall back to Discogs if Deezer doesn't have the artist
    searchDeezer();
}

void ArtistCoverDownloader::searchDeezer()
{
    if(m_cancelled || !m_coreContext || !m_coreContext->networkAccess) {
        finishCurrent(false);
        return;
    }

    m_triedDeezer = true;

    QUrl url{QString::fromLatin1(DeezerSearchUrl)};
    QUrlQuery query;
    query.addQueryItem(u"q"_s, m_currentArtist);
    query.addQueryItem(u"output"_s, u"json"_s);
    url.setQuery(query);

    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "AlbumMosaicPlugin/1.0 (dewi)");

    m_reply = m_coreContext->networkAccess->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &ArtistCoverDownloader::handleDeezerReply);
}

void ArtistCoverDownloader::handleDeezerReply()
{
    if(!m_reply || m_cancelled) {
        finishCurrent(false);
        return;
    }

    if(m_reply->error() != QNetworkReply::NoError) {
        qWarning() << "[ArtistCoverDownloader] Deezer search error for" << m_currentArtist << ":" << m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        // Fall back to Discogs on network error
        searchArtist();
        return;
    }

    const QByteArray data = m_reply->readAll();
    m_reply->deleteLater();
    m_reply = nullptr;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject obj = doc.object();
    const QJsonArray results = obj.value("data"_L1).toArray();

    if(results.isEmpty()) {
        // No Deezer results — try Discogs
        searchArtist();
        return;
    }

    // Find the first result with a usable image (picture_xl or picture_big)
    for(const auto& resultVal : results) {
        const QJsonObject result = resultVal.toObject();
        const QString name = result.value("name"_L1).toString();
        // Skip if the artist name doesn't match (case-insensitive)
        if(!name.contains(m_currentArtist, Qt::CaseInsensitive)
           && !m_currentArtist.contains(name, Qt::CaseInsensitive)) {
            continue;
        }

        // Prefer picture_xl (1000x1000), fall back to picture_big (500x500)
        QString imageUrl = result.value("picture_xl"_L1).toString();
        if(imageUrl.isEmpty()) {
            imageUrl = result.value("picture_big"_L1).toString();
        }
        if(imageUrl.isEmpty()) {
            continue;
        }

        downloadImage(imageUrl);
        return;
    }

    // Deezer had results but none matched or had images — try Discogs
    searchArtist();
}

void ArtistCoverDownloader::searchArtist()
{
    if(m_cancelled || !m_coreContext || !m_coreContext->networkAccess) {
        finishCurrent(false);
        return;
    }

    // Throttle Discogs requests (1 req/sec rate limit)
    QTimer::singleShot(1100, this, [this]() {
        if(m_cancelled) return;

        QUrl url{QString::fromLatin1(SearchUrl)};
        QUrlQuery query;
        query.addQueryItem(u"key"_s, m_apiKey);
        query.addQueryItem(u"secret"_s, m_secret);
        query.addQueryItem(u"type"_s, u"artist"_s);
        query.addQueryItem(u"q"_s, m_currentArtist);
        url.setQuery(query);

        QNetworkRequest req{url};
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setRawHeader("User-Agent", "AlbumMosaicPlugin/1.0 (dewi)");

        m_reply = m_coreContext->networkAccess->get(req);
        connect(m_reply, &QNetworkReply::finished, this, &ArtistCoverDownloader::handleSearchReply);
    });
}

void ArtistCoverDownloader::handleSearchReply()
{
    if(!m_reply || m_cancelled) {
        finishCurrent(false);
        return;
    }

    if(m_reply->error() != QNetworkReply::NoError) {
        qWarning() << "[ArtistCoverDownloader] Search error for" << m_currentArtist << ":" << m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        finishCurrent(false);
        return;
    }

    const QByteArray data = m_reply->readAll();
    m_reply->deleteLater();
    m_reply = nullptr;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject obj = doc.object();
    const QJsonArray results = obj.value("results"_L1).toArray();

    if(results.isEmpty()) {
        qDebug() << "[ArtistCoverDownloader] No results for" << m_currentArtist;
        finishCurrent(false);
        return;
    }

    // Take the first result (most relevant)
    const QJsonObject firstResult = results.at(0).toObject();
    const QString resourceUrl = firstResult.value("resource_url"_L1).toString();

    if(resourceUrl.isEmpty()) {
        finishCurrent(false);
        return;
    }

    fetchArtistProfile(resourceUrl);
}

void ArtistCoverDownloader::fetchArtistProfile(const QString& resourceUrl)
{
    if(m_cancelled || !m_coreContext || !m_coreContext->networkAccess) {
        finishCurrent(false);
        return;
    }

    QUrl url{resourceUrl};
    QUrlQuery query;
    query.addQueryItem(u"key"_s, m_apiKey);
    query.addQueryItem(u"secret"_s, m_secret);
    url.setQuery(query);

    QNetworkRequest req{url};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "AlbumMosaicPlugin/1.0 (dewi)");

    m_reply = m_coreContext->networkAccess->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &ArtistCoverDownloader::handleProfileReply);
}

void ArtistCoverDownloader::handleProfileReply()
{
    if(!m_reply || m_cancelled) {
        finishCurrent(false);
        return;
    }

    if(m_reply->error() != QNetworkReply::NoError) {
        qWarning() << "[ArtistCoverDownloader] Profile error for" << m_currentArtist << ":" << m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        finishCurrent(false);
        return;
    }

    const QByteArray data = m_reply->readAll();
    m_reply->deleteLater();
    m_reply = nullptr;

    const QJsonDocument doc = QJsonDocument::fromJson(data);
    const QJsonObject obj = doc.object();
    const QJsonArray images = obj.value("images"_L1).toArray();

    if(images.isEmpty()) {
        qDebug() << "[ArtistCoverDownloader] No images for" << m_currentArtist;
        finishCurrent(false);
        return;
    }

    // Find the first image >=300x300 with aspect ratio >=0.75 (same criteria as fooyin's DiscogsArtwork)
    for(const auto& imageVal : images) {
        const QJsonObject imageObj = imageVal.toObject();
        const QString imageUrl = imageObj.value("resource_url"_L1).toString();
        const auto width = imageObj.value("width"_L1).toVariant().toFloat();
        const auto height = imageObj.value("height"_L1).toVariant().toFloat();

        if(imageUrl.isEmpty() || width < 300 || height < 300) {
            continue;
        }

        const float aspectRatio = std::min(width, height) / std::max(width, height);
        if(aspectRatio < 0.75F) {
            continue;
        }

        downloadImage(imageUrl);
        return;
    }

    qDebug() << "[ArtistCoverDownloader] No suitable image for" << m_currentArtist;
    finishCurrent(false);
}

void ArtistCoverDownloader::downloadImage(const QString& imageUrl)
{
    if(m_cancelled || !m_coreContext || !m_coreContext->networkAccess) {
        finishCurrent(false);
        return;
    }

    QNetworkRequest req{QUrl{imageUrl}};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setRawHeader("User-Agent", "AlbumMosaicPlugin/1.0 (dewi)");

    m_reply = m_coreContext->networkAccess->get(req);
    connect(m_reply, &QNetworkReply::finished, this, &ArtistCoverDownloader::handleImageReply);
}

void ArtistCoverDownloader::handleImageReply()
{
    if(!m_reply || m_cancelled) {
        finishCurrent(false);
        return;
    }

    if(m_reply->error() != QNetworkReply::NoError) {
        qWarning() << "[ArtistCoverDownloader] Image download error for" << m_currentArtist << ":" << m_reply->errorString();
        m_reply->deleteLater();
        m_reply = nullptr;
        finishCurrent(false);
        return;
    }

    const QByteArray data = m_reply->readAll();
    m_reply->deleteLater();
    m_reply = nullptr;

    if(data.isEmpty()) {
        finishCurrent(false);
        return;
    }

    saveCover(data);
}

void ArtistCoverDownloader::saveCover(const QByteArray& data)
{
    if(!m_currentTrack.isValid() || m_currentTrack.filepath().isEmpty()) {
        qDebug() << "[ArtistCoverDownloader] No track path for" << m_currentArtist << ", skipping save";
        finishCurrent(false);
        return;
    }

    // Save to TWO locations:
    // 1. Plugin cache (~/.local/share/fooyin/artistcovers/<md5>.jpg) — the plugin
    //    always finds it via fallback, regardless of which track is representative.
    //    Uses Lollypop-style md5 keying, no dependency on directory layout.
    // 2. Artist's parent directory (artist.jpg) — convention for other players
    //    (MusicBee, MediaMonkey). Fooyin can find it if the user adds
    //    %path%/../artist.* to the artist paths config.
    // NOTE: We do NOT save to the track's directory — fooyin's %path%/artist.*
    // would find it for ALL artists whose tracks share that directory (e.g.
    // compilation albums), causing the same cover to appear for different artists.
    const QString trackDir = QFileInfo(m_currentTrack.filepath()).absolutePath();
    const QString artistDir = QDir(trackDir).absoluteFilePath("..");
    const QString artistPath = QDir(artistDir).absoluteFilePath("artist.jpg");

    // Plugin cache path
    const QString cacheDir = QDir::homePath() + "/.local/share/fooyin/artistcovers";
    QDir().mkpath(cacheDir);
    const QString cachePath = cacheDir + "/" +
        QString(QCryptographicHash::hash(m_currentArtist.toUtf8(), QCryptographicHash::Md5).toHex()) + ".jpg";

    bool saved = false;

    // Save to plugin cache
    QFile cacheFile{cachePath};
    if(cacheFile.open(QIODevice::WriteOnly) && cacheFile.write(data) == data.size()) {
        cacheFile.close();
        qDebug() << "[ArtistCoverDownloader] Cached cover for" << m_currentArtist << "to" << cachePath;
        saved = true;
    }

    // Save to artist's parent directory
    if(artistDir != trackDir) { // avoid writing the same file twice
        QFile artistFile{artistPath};
        if(artistFile.open(QIODevice::WriteOnly) && artistFile.write(data) == data.size()) {
            artistFile.close();
            qDebug() << "[ArtistCoverDownloader] Saved cover for" << m_currentArtist << "to" << artistPath << "(artist dir)";
            saved = true;
        }
    }

    if(!saved) {
        qWarning() << "[ArtistCoverDownloader] Failed to save cover for" << m_currentArtist;
        finishCurrent(false);
        return;
    }

    // Invalidate the CoverRepository cache for this track so fooyin picks up the new file
    if(m_guiContext && m_guiContext->coverRepository) {
        m_guiContext->coverRepository->removeFromCache(m_currentTrack);
    }

    emit coverDownloaded(m_currentArtist);
    finishCurrent(true);
}

void ArtistCoverDownloader::finishCurrent(bool success)
{
    m_doneCount++;
    emit progress(m_doneCount, m_totalCount);

    if(!success) {
        qDebug() << "[ArtistCoverDownloader] Failed to download cover for" << m_currentArtist;
    }

    processNext();
}
