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

#pragma once

#include <core/track.h>
#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugin.h>

#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>

class QNetworkReply;
class QTimer;

class ArtistCoverDownloader : public QObject
{
    Q_OBJECT

public:
    explicit ArtistCoverDownloader(Fooyin::CorePluginContext* coreContext,
                                   Fooyin::GuiPluginContext* guiContext,
                                   QObject* parent = nullptr);
    ~ArtistCoverDownloader() override;

    // Download covers for all artists in the list that don't already have one.
    // Runs in the background with 1 req/sec throttling (Discogs rate limit).
    void downloadMissing(const QList<QPair<QString, Fooyin::Track>>& artists);

    // Download a single artist's cover immediately (user-triggered from context menu).
    void downloadSingleArtist(const QString& artistName, const Fooyin::Track& track);

    void cancel();

signals:
    // Emitted when a cover has been downloaded and saved for @p artist.
    void coverDownloaded(const QString& artist);
    // Progress: @p done out of @p total artists processed.
    void progress(int done, int total);
    // All queued downloads finished.
    void finished();

private:
    void processNext();
    // Deezer: fast single-request search (primary source)
    void searchDeezer();
    void handleDeezerReply();
    // Discogs: slower 3-request flow (fallback)
    void searchArtist();
    void handleSearchReply();
    void fetchArtistProfile(const QString& resourceUrl);
    void handleProfileReply();
    void downloadImage(const QString& imageUrl);
    void handleImageReply();
    void saveCover(const QByteArray& data);
    void finishCurrent(bool success);

    Fooyin::CorePluginContext* m_coreContext;
    Fooyin::GuiPluginContext* m_guiContext;

    // Deezer API (no auth needed, returns image URL directly in search response)
    static constexpr const char* DeezerSearchUrl = "https://api.deezer.com/search/artist/";

    // Discogs API credentials (same as fooyin's DiscogsArtwork source)
    static constexpr const char* SearchUrl = "https://api.discogs.com/database/search";
    static constexpr const char* AccessKeyB64 = "RmxzdXNwWkN5V2liZHJ3aHh1YlA=";
    static constexpr const char* SecretB64 = "Z0NDTU1LYm9SblpnTmpOU2VxQWR6ek1aZVdxZkJZTVE=";

    QString m_apiKey;
    QString m_secret;

    // Queue of artists to process
    QList<QPair<QString, Fooyin::Track>> m_queue;
    int m_totalCount{0};
    int m_doneCount{0};
    bool m_running{false};
    bool m_triedDeezer{false};

    // Current artist being processed
    QString m_currentArtist;
    Fooyin::Track m_currentTrack;
    QNetworkReply* m_reply{nullptr};
    QTimer* m_throttleTimer{nullptr};
    bool m_cancelled{false};
    // Rep-track dirs shared by several artists in the last downloadMissing() call —
    // covers must not be written into such dirs' parents (ambiguous location).
    QSet<QString> m_sharedRepDirs;
};
