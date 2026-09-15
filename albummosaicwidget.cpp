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
 *
 */

#include "albummosaicwidget.h"
#include "albummosaicsettingsdialog.h"
#include "artistcoverdownloader.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QRandomGenerator>
#include <QTimer>
#include <QSet>
#include <set>
#include <QDebug>
#include <algorithm>
#include <numeric>
#include <QLinearGradient>
#include <QFont>
#include <QPainterPath>
#include <QMenu>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cmath>
#include <core/library/musiclibrary.h>
#include <core/library/tracksort.h>
#include <core/player/playercontroller.h>
#include <core/playlist/playlisthandler.h>
#include <core/playlist/playlist.h>
#include <core/track.h>
#include <gui/coverprovider.h>
#include <gui/coverartworktypes.h>
#include <gui/trackselectioncontroller.h>
#include <gui/propertiesdialog.h>
#include <gui/widgets/tooltip.h>
#include <utils/actions/widgetcontext.h>
#include <core/plugins/coreplugincontext.h>
#include <utils/settings/settingsmanager.h>

QString AlbumMosaicWidget::displayModeKey(DisplayMode m)
{
    switch(m) {
        case DisplayMode::Artist: return QStringLiteral("Artist");
        default:                  return QStringLiteral("Album");
    }
}

AlbumMosaicWidget::DisplayMode AlbumMosaicWidget::displayModeFromKey(const QString& s)
{
    if(s == QLatin1String("Artist")) return DisplayMode::Artist;
    return DisplayMode::Album;
}

QString AlbumMosaicWidget::animTypeKey(AnimType t)
{
    switch(t) {
        case AnimType::Crossfade: return QStringLiteral("Crossfade");
        case AnimType::Slide:     return QStringLiteral("Slide");
        case AnimType::Zoom:      return QStringLiteral("Zoom");
        case AnimType::PageCurl:  return QStringLiteral("PageCurl");
        case AnimType::Random:    return QStringLiteral("Random");
        default:                  return QStringLiteral("Flip3D");
    }
}

AlbumMosaicWidget::AnimType AlbumMosaicWidget::animTypeFromKey(const QString& s)
{
    if(s == QLatin1String("Crossfade")) return AnimType::Crossfade;
    if(s == QLatin1String("Slide"))     return AnimType::Slide;
    if(s == QLatin1String("Zoom"))      return AnimType::Zoom;
    if(s == QLatin1String("PageCurl"))  return AnimType::PageCurl;
    if(s == QLatin1String("Random"))    return AnimType::Random;
    return AnimType::Flip3D;
}

QString AlbumMosaicWidget::animSpeedKey(AnimSpeed s)
{
    switch(s) {
        case AnimSpeed::Fast: return QStringLiteral("Fast");
        case AnimSpeed::Slow: return QStringLiteral("Slow");
        default:              return QStringLiteral("Medium");
    }
}

AlbumMosaicWidget::AnimSpeed AlbumMosaicWidget::animSpeedFromKey(const QString& s)
{
    if(s == QLatin1String("Fast")) return AnimSpeed::Fast;
    if(s == QLatin1String("Slow")) return AnimSpeed::Slow;
    return AnimSpeed::Medium;
}

QString AlbumMosaicWidget::animScopeKey(AnimScope s)
{
    switch(s) {
        case AnimScope::Multiple: return QStringLiteral("Multiple");
        case AnimScope::Wave:     return QStringLiteral("Wave");
        default:                  return QStringLiteral("Single");
    }
}

AlbumMosaicWidget::AnimScope AlbumMosaicWidget::animScopeFromKey(const QString& s)
{
    if(s == QLatin1String("Multiple")) return AnimScope::Multiple;
    if(s == QLatin1String("Wave"))     return AnimScope::Wave;
    return AnimScope::Single;
}

QString AlbumMosaicWidget::sortModeKey(SortMode m)
{
    switch(m) {
        case SortMode::Year:      return QStringLiteral("Year");
        case SortMode::YearDesc:  return QStringLiteral("YearDesc");
        case SortMode::Rating:    return QStringLiteral("Rating");
        case SortMode::PlayCount: return QStringLiteral("PlayCount");
        case SortMode::Recent:    return QStringLiteral("Recent");
        case SortMode::Alphabetical: return QStringLiteral("Alphabetical");
        default:                  return QStringLiteral("Random");
    }
}

AlbumMosaicWidget::SortMode AlbumMosaicWidget::sortModeFromKey(const QString& s)
{
    if(s == QLatin1String("Year"))         return SortMode::Year;
    if(s == QLatin1String("YearDesc"))     return SortMode::YearDesc;
    if(s == QLatin1String("Rating"))       return SortMode::Rating;
    if(s == QLatin1String("PlayCount"))    return SortMode::PlayCount;
    if(s == QLatin1String("Recent"))       return SortMode::Recent;
    if(s == QLatin1String("Alphabetical")) return SortMode::Alphabetical;
    return SortMode::Random;
}

// Interval between animation batches: speed base × scope factor.
// More simultaneous animations = longer pause to reduce CPU.
int AlbumMosaicWidget::animIntervalMs() const
{
    int base = 3000;
    switch(m_animSpeed) {
        case AnimSpeed::Fast: base = 1500; break;
        case AnimSpeed::Slow: base = 6000; break;
        default: break;
    }
    switch(m_animScope) {
        case AnimScope::Multiple: return static_cast<int>(base * 1.5);
        case AnimScope::Wave:     return base * 2;
        default:                  return base;
    }
}

AlbumMosaicWidget::AlbumMosaicWidget(Fooyin::GuiPluginContext* guiContext, Fooyin::CorePluginContext* coreContext, Fooyin::CoverProvider* coverProvider, ArtistCoverDownloader* artistCoverDownloader, QWidget* parent)
    : FyWidget{parent}
    , m_guiContext{guiContext}
    , m_coreContext{coreContext}
    , m_coverProvider{coverProvider}
    , m_artistCoverDownloader{artistCoverDownloader}
    , m_animTimer{new QTimer(this)}
{
    if(m_coreContext && m_coreContext->settingsManager) {
        auto* settings = m_coreContext->settingsManager;
        m_enableAnim = settings->value(QStringLiteral("AlbumMosaic/EnableAnim")).toBool();
        m_columnCount = settings->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt();
        m_genreFilter = settings->value(QStringLiteral("AlbumMosaic/GenreFilter")).toString();
        m_artistFilter = settings->value(QStringLiteral("AlbumMosaic/ArtistFilter")).toString();
        m_animType = animTypeFromKey(settings->value(QStringLiteral("AlbumMosaic/AnimType")).toString());
        m_animSpeed = animSpeedFromKey(settings->value(QStringLiteral("AlbumMosaic/AnimSpeed")).toString());
        m_animScope = animScopeFromKey(settings->value(QStringLiteral("AlbumMosaic/AnimScope")).toString());
        m_sortMode = sortModeFromKey(settings->value(QStringLiteral("AlbumMosaic/SortMode")).toString());
        m_bgColor = QColor(settings->value(QStringLiteral("AlbumMosaic/BgColor")).toString());
        if(!m_bgColor.isValid()) m_bgColor = Qt::black;
        m_displayMode = displayModeFromKey(settings->value(QStringLiteral("AlbumMosaic/DisplayMode")).toString());
        m_autoDownloadArtistCovers = settings->value(QStringLiteral("AlbumMosaic/AutoDownloadArtistCovers")).toBool();
    }

    setMouseTracking(true);
    // Allow Fooyin's SearchBar to connect to this widget and send search events
    setFeature(FyWidget::Search);
    connect(m_animTimer, &QTimer::timeout, this, &AlbumMosaicWidget::triggerAnimation);
    if(m_enableAnim) {
        m_animTimer->start(animIntervalMs());
    }

    // Store placeholder cache key for cover comparison
    if(m_coverProvider) {
        m_placeholderCacheKey = m_coverProvider->placeholderCover().cacheKey();
    }

    // Fade-in timer — advances cover fade progress at ~20fps
    m_fadeTimer = new QTimer(this);
    m_fadeTimer->setInterval(50);
    connect(m_fadeTimer, &QTimer::timeout, this, &AlbumMosaicWidget::advanceFade);

    // Tooltip hide timer — delays hiding the tooltip for a smoother feel
    m_toolTipTimer = new QTimer(this);
    m_toolTipTimer->setSingleShot(true);
    m_toolTipTimer->setInterval(500);
    connect(m_toolTipTimer, &QTimer::timeout, this, [this]() {
        if(m_toolTip) m_toolTip->hide();
    });

    // Fooyin styled ToolTip — parented to this widget so it is destroyed with it
    m_toolTip = new Fooyin::ToolTip(this);
    m_toolTip->setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint);
    // Semi-transparent dark style: black bg at 50% opacity, white text, subtle border
    QPalette tipPalette = m_toolTip->palette();
    tipPalette.setColor(QPalette::Highlight, QColor(0, 0, 0, 128));
    tipPalette.setColor(QPalette::HighlightedText, QColor(255, 255, 255, 220));
    m_toolTip->setPalette(tipPalette);
    m_toolTip->hide();

    // TrackSorter for idiomatic Fooyin sorting
    m_trackSorter = std::make_unique<Fooyin::TrackSorter>();

    // WidgetContext for TrackSelectionController integration
    // This allows Fooyin's standard context menu actions (Play, Queue, Add to Playlist) to work
    m_widgetContext = new Fooyin::WidgetContext(this, Fooyin::Context{Fooyin::Constants::Context::Global}, this);

    // Inline sort bar — minimal combo in top-right corner
    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem(tr("Random"), QStringLiteral("Random"));
    m_sortCombo->addItem(tr("A-Z"), QStringLiteral("Alphabetical"));
    m_sortCombo->addItem(tr("Year ↓"), QStringLiteral("YearDesc"));
    m_sortCombo->addItem(tr("Year ↑"), QStringLiteral("Year"));
    m_sortCombo->addItem(tr("Rating"), QStringLiteral("Rating"));
    m_sortCombo->addItem(tr("Plays"), QStringLiteral("PlayCount"));
    m_sortCombo->addItem(tr("Recent"), QStringLiteral("Recent"));
    m_sortCombo->setToolTip(tr("Sort albums"));
    m_sortCombo->setFixedSize(90, 24);
    m_sortCombo->setStyleSheet(QStringLiteral(
        "QComboBox { background-color: rgba(0,0,0,180); color: white; border: 1px solid #444; border-radius: 3px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; width: 16px; }"
        "QComboBox QAbstractItemView { background-color: #222; color: white; selection-background-color: #444; }"));
    // Restore current sort
    QString currentSort = QStringLiteral("Random");
    if(m_coreContext && m_coreContext->settingsManager) {
        currentSort = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/SortMode")).toString();
    }
    int idx = m_sortCombo->findData(currentSort);
    if(idx >= 0) m_sortCombo->setCurrentIndex(idx);
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AlbumMosaicWidget::onSortChanged);

    // Bug 8: Connect to MusicLibrary signals for dynamic updates
    if(m_coreContext && m_coreContext->library) {
        connect(m_coreContext->library, &Fooyin::MusicLibrary::tracksLoaded, this, [this](const Fooyin::TrackList&) {
            loadAlbumMetadata();
        });
        connect(m_coreContext->library, &Fooyin::MusicLibrary::tracksAdded, this, [this](const Fooyin::TrackList&) {
            loadAlbumMetadata();
        });
        connect(m_coreContext->library, &Fooyin::MusicLibrary::tracksDeleted, this, [this](const Fooyin::TrackList&) {
            loadAlbumMetadata();
        });
    }

    // Feature: Highlight currently playing album
    if(m_coreContext && m_coreContext->playerController) {
        connect(m_coreContext->playerController, &Fooyin::PlayerController::currentTrackChanged, this, [this](const Fooyin::Track& track) {
            m_currentPlayingAlbum = track.album();
            m_currentPlayingArtist = track.albumArtist();
            // Pre-resolve the index once — avoids 2 QString compares
            // per visible cell per repaint. Key differs by display mode.
            if(m_displayMode == DisplayMode::Artist) {
                m_playingAlbumIndex = m_albumKeyToIndex.value(m_currentPlayingArtist, -1);
            } else {
                m_playingAlbumIndex = m_albumKeyToIndex.value(
                    m_currentPlayingAlbum + "|" + m_currentPlayingArtist, -1);
            }
            update();
        });
    }

    // Connect to the shared artist cover downloader (plugin-level singleton)
    if(m_artistCoverDownloader) {
        connect(m_artistCoverDownloader, &ArtistCoverDownloader::coverDownloaded,
                this, &AlbumMosaicWidget::onArtistCoverDownloaded);
    }

    // loadAlbumMetadata() builds m_albums, then calls randomizeGrid()
    // which builds m_albumOrder and calls updateMosaic()
    // Delayed to avoid blocking the event loop during startup
    QTimer::singleShot(100, this, &AlbumMosaicWidget::loadAlbumMetadata);
}

AlbumMosaicWidget::~AlbumMosaicWidget()
{
    delete m_animTimer;
}

QString AlbumMosaicWidget::name() const
{
    return tr("Album Mosaic");
}

QString AlbumMosaicWidget::layoutName() const
{
    return QStringLiteral("AlbumMosaic");
}

void AlbumMosaicWidget::loadAlbumMetadata()
{
    if(m_displayMode == DisplayMode::Artist) {
        loadArtists();
    } else {
        loadAlbums();
    }
}

void AlbumMosaicWidget::loadAlbums()
{
    if(!m_coreContext || !m_coreContext->library) {
        return;
    }

    invalidateScaledCache();

    Fooyin::TrackList tracks = m_coreContext->library->tracks();

    if(tracks.empty()) {
        QTimer::singleShot(2000, this, &AlbumMosaicWidget::loadAlbumMetadata);
        return;
    }

    // Track whether the album set actually changed — avoids regenerating
    // the grid order on redundant calls (startup, tracksLoaded, loadSettings)
    const int oldAlbumCount = m_albums.size();

    m_albums.clear();
    m_albumTracksCache.clear();
    m_albumKeyToIndex.clear();
    m_pendingPreload.clear(); // Album indices are about to become invalid
    m_activeAnims.clear();    // Active anims hold order/cell indices into the old set
    QSet<QString> uniqueAlbums;

    for(const Fooyin::Track& track : tracks) {
        const QString album = track.album();
        const QString albumArtist = track.albumArtist();

        if(album.isEmpty() || albumArtist.isEmpty()) {
            continue;
        }

        // Skip if genre filter is set and track doesn't match
        if(!m_genreFilter.isEmpty() && track.hasGenres()) {
            bool genreMatch = false;
            for(const QString& genre : track.genres()) {
                if(genre.contains(m_genreFilter, Qt::CaseInsensitive) || m_genreFilter.contains(genre, Qt::CaseInsensitive)) {
                    genreMatch = true;
                    break;
                }
            }
            if(!genreMatch) {
                continue;
            }
        } else if(!m_genreFilter.isEmpty() && !track.hasGenres()) {
            continue;
        }

        // Skip if artist filter is set and track doesn't match
        if(!m_artistFilter.isEmpty()) {
            if(!track.albumArtist().contains(m_artistFilter, Qt::CaseInsensitive)
               && !track.artist().contains(m_artistFilter, Qt::CaseInsensitive)) {
                continue;
            }
        }

        // Skip if search query is set and track doesn't match
        if(!m_searchQuery.isEmpty()) {
            if(!album.contains(m_searchQuery, Qt::CaseInsensitive)
               && !albumArtist.contains(m_searchQuery, Qt::CaseInsensitive)) {
                continue;
            }
        }

        const QString albumKey = album + "|" + albumArtist;
        if(!uniqueAlbums.contains(albumKey)) {
            uniqueAlbums.insert(albumKey);

            AlbumInfo info;
            info.album = album;
            info.albumArtist = albumArtist;
            info.filePath = track.filepath();
            info.track = track;
            m_albums.append(info);
        }

        // Bug 3: Cache tracks per album while scanning
        m_albumTracksCache[albumKey].push_back(track);
    }

    // Only reshuffle and regenerate the grid order if the album set changed.
    // This prevents the grid from flickering/replacing at startup when
    // loadAlbumMetadata() is called multiple times (constructor + tracksLoaded).
    const bool albumSetChanged = (m_albums.size() != oldAlbumCount);

    if(albumSetChanged) {
        std::shuffle(m_albums.begin(), m_albums.end(), *QRandomGenerator::global());
        qDebug() << "[AlbumMosaic] Loaded" << m_albums.size() << "albums from Fooyin library";
        randomizeGrid();
    } else {
        updateMosaic();
    }

    // Rebuild albumKey->index map AFTER the shuffle (indices moved)
    m_albumKeyToIndex.clear();
    m_albumKeyToIndex.reserve(m_albums.size());
    for(int i = 0; i < m_albums.size(); ++i) {
        m_albumKeyToIndex.insert(m_albums[i].album + "|" + m_albums[i].albumArtist, i);
    }
    // Re-resolve the playing album index against the rebuilt map
    m_playingAlbumIndex = m_albumKeyToIndex.value(
        m_currentPlayingAlbum + "|" + m_currentPlayingArtist, -1);

    // Bug 2: Disconnect before reconnect to avoid signal leak
    if(m_coverProvider) {
        disconnect(m_coverProvider, &Fooyin::CoverProvider::coverAdded, this, nullptr);
        connect(m_coverProvider, &Fooyin::CoverProvider::coverAdded, this, [this](const Fooyin::Track& track) {
            // O(1) lookup via albumKey — avoids scanning all albums per cover
            const QString albumKey = track.album() + "|" + track.albumArtist();
            const int albumIndex = m_albumKeyToIndex.value(albumKey, -1);
            if(albumIndex >= 0) {
                m_scaledCache.remove(albumIndex);
                // If this cover was requested by the animation preload, put it
                // into the scaled cache now. Otherwise the swap-candidate pool
                // stays empty until the user scrolls (preloaded albums are
                // never painted, so getCoverForPaint is never re-called).
                if(m_pendingPreload.remove(albumIndex) && !m_coverPositions.isEmpty()) {
                    const QSize cellSize = m_coverPositions[0].size();
                    const auto coverSize = Fooyin::CoverProvider::findThumbnailSize(cellSize);
                    getCoverForPaint(albumIndex, cellSize, coverSize);
                }
                // Fade-in only for visible covers — during the initial scan,
                // coverAdded fires for hundreds of off-screen albums.
                if(m_currentGridIndices.contains(albumIndex)) {
                    m_coverFadeProgress[albumIndex] = 0;
                    if(m_fadeTimer && !m_fadeTimer->isActive()) {
                        m_fadeTimer->start();
                    }
                }
                update();
            }
        });
    }
}

void AlbumMosaicWidget::loadArtists()
{
    if(!m_coreContext || !m_coreContext->library) {
        return;
    }

    invalidateScaledCache();

    Fooyin::TrackList tracks = m_coreContext->library->tracks();

    if(tracks.empty()) {
        QTimer::singleShot(2000, this, &AlbumMosaicWidget::loadAlbumMetadata);
        return;
    }

    const int oldCount = m_albums.size();

    m_albums.clear();
    m_albumTracksCache.clear();
    m_albumKeyToIndex.clear();
    m_pendingPreload.clear();
    m_activeAnims.clear();
    QSet<QString> uniqueArtists;

    for(const Fooyin::Track& track : tracks) {
        const QString albumArtist = track.albumArtist();

        if(albumArtist.isEmpty()) {
            continue;
        }

        // Skip if genre filter is set and track doesn't match
        if(!m_genreFilter.isEmpty() && track.hasGenres()) {
            bool genreMatch = false;
            for(const QString& genre : track.genres()) {
                if(genre.contains(m_genreFilter, Qt::CaseInsensitive) || m_genreFilter.contains(genre, Qt::CaseInsensitive)) {
                    genreMatch = true;
                    break;
                }
            }
            if(!genreMatch) {
                continue;
            }
        } else if(!m_genreFilter.isEmpty() && !track.hasGenres()) {
            continue;
        }

        // Skip if artist filter is set and track doesn't match
        if(!m_artistFilter.isEmpty()) {
            if(!track.albumArtist().contains(m_artistFilter, Qt::CaseInsensitive)
               && !track.artist().contains(m_artistFilter, Qt::CaseInsensitive)) {
                continue;
            }
        }

        // Skip if search query is set and track doesn't match
        if(!m_searchQuery.isEmpty()) {
            if(!albumArtist.contains(m_searchQuery, Qt::CaseInsensitive)) {
                continue;
            }
        }

        if(!uniqueArtists.contains(albumArtist)) {
            uniqueArtists.insert(albumArtist);

            AlbumInfo info;
            info.album = albumArtist; // Display name = artist name
            info.albumArtist = albumArtist;
            info.filePath = track.filepath();
            info.track = track; // Representative track for CoverProvider
            m_albums.append(info);
        }

        // Cache ALL tracks for this artist (for click-to-play)
        m_albumTracksCache[albumArtist].push_back(track);
    }

    const bool setChanged = (m_albums.size() != oldCount);

    if(setChanged) {
        std::shuffle(m_albums.begin(), m_albums.end(), *QRandomGenerator::global());
        qDebug() << "[AlbumMosaic] Loaded" << m_albums.size() << "artists from Fooyin library";
        randomizeGrid();
    } else {
        updateMosaic();
    }

    // Rebuild key->index map (key = albumArtist only in Artist mode)
    m_albumKeyToIndex.clear();
    m_albumKeyToIndex.reserve(m_albums.size());
    for(int i = 0; i < m_albums.size(); ++i) {
        m_albumKeyToIndex.insert(m_albums[i].albumArtist, i);
    }
    // Re-resolve the playing artist index
    m_playingAlbumIndex = m_albumKeyToIndex.value(m_currentPlayingArtist, -1);

    // coverAdded handler — key is albumArtist only in Artist mode.
    // Distinguishes add vs remove via trackHasCover so fooyin's native
    // artwork actions (attach/search/remove) stay in sync with our tiles.
    if(m_coverProvider) {
        disconnect(m_coverProvider, &Fooyin::CoverProvider::coverAdded, this, nullptr);
        connect(m_coverProvider, &Fooyin::CoverProvider::coverAdded, this, [this](const Fooyin::Track& track) {
            const QString artistKey = track.albumArtist();
            const int artistIndex = m_albumKeyToIndex.value(artistKey, -1);
            if(artistIndex < 0) {
                return;
            }
            m_scaledCache.remove(artistIndex);
            if(m_pendingPreload.remove(artistIndex) && !m_coverPositions.isEmpty()) {
                const QSize cellSize = m_coverPositions[0].size();
                const auto coverSize = Fooyin::CoverProvider::findThumbnailSize(cellSize);
                getCoverForPaint(artistIndex, cellSize, coverSize);
            }
            if(m_currentGridIndices.contains(artistIndex)) {
                m_coverFadeProgress[artistIndex] = 0;
                if(m_fadeTimer && !m_fadeTimer->isActive()) {
                    m_fadeTimer->start();
                }
            }
            update();

            // Sync our own cover store with fooyin's artwork state for this track.
            // Dedup: coverAdded fires once per selected track — only run the
            // async check once per artist at a time.
            if(m_coverSyncPending.contains(artistKey)) {
                return;
            }
            m_coverSyncPending.insert(artistKey);
            const QString cacheDir = QDir::homePath() + "/.local/share/fooyin/artistcovers";
            const QString md5 = QString(QCryptographicHash::hash(artistKey.toUtf8(), QCryptographicHash::Md5).toHex());
            const QString cachePath = cacheDir + "/" + md5 + ".jpg";
            m_coverProvider->trackHasCover(track, Fooyin::Track::Cover::Artist)
                .then(this, [this, artistKey, cachePath, track](bool hasCover) {
                    m_coverSyncPending.remove(artistKey);
                    const int idx = m_albumKeyToIndex.value(artistKey, -1);
                    if(idx < 0 || idx >= m_albums.size()) {
                        return;
                    }
                    if(!hasCover) {
                        // Artwork removed in fooyin — drop our stored copies so the
                        // tile clears. Only delete the parent-dir artist.jpg when it
                        // is byte-identical to our cached copy (i.e. we wrote it);
                        // never touch a file the user placed there themselves.
                        QByteArray cached;
                        if(QFile cf{cachePath}; cf.open(QIODevice::ReadOnly)) {
                            cached = cf.readAll();
                        }
                        if(!cached.isEmpty()) {
                            const QString trackDir = QFileInfo(m_albums[idx].track.filepath()).absolutePath();
                            const QString artistJpg
                                = QDir(QDir(trackDir).absoluteFilePath("..")).absoluteFilePath("artist.jpg");
                            if(QFile af{artistJpg}; af.open(QIODevice::ReadOnly) && af.readAll() == cached) {
                                af.close();
                                QFile::remove(artistJpg);
                            }
                        }
                        QFile::remove(cachePath);
                        m_scaledCache.remove(idx);
                        update();
                    }
                    else {
                        // Artwork attached via fooyin's dialog — pull it from the
                        // changed track (which may live in a different dir than our
                        // representative track) into our stable md5 cache. Pixel-
                        // compare against the existing cache so we only write when
                        // the user actually attached a different image.
                        m_coverProvider->trackCoverThumbnailAsync(track, QSize{512, 512},
                                                                  Fooyin::Track::Cover::Artist)
                            .then(this, [this, cachePath, artistKey](const QPixmap& pix) {
                                const int idx2 = m_albumKeyToIndex.value(artistKey, -1);
                                const auto artistPlaceholderKey
                                    = m_coverProvider->placeholderCover(Fooyin::Track::Cover::Artist).cacheKey();
                                if(idx2 < 0 || pix.isNull() || pix.cacheKey() == m_placeholderCacheKey
                                   || pix.cacheKey() == artistPlaceholderKey) {
                                    return;
                                }
                                const QPixmap existing{cachePath};
                                if(!existing.isNull() && existing.toImage() == pix.toImage()) {
                                    return;
                                }
                                QDir().mkpath(QFileInfo(cachePath).absolutePath());
                                if(pix.save(cachePath, "JPG", 90)) {
                                    m_scaledCache.remove(idx2);
                                    update();
                                }
                            });
                    }
                });
        });
    }

    // Auto-download missing artist covers if enabled
    if(m_autoDownloadArtistCovers && setChanged && !m_downloadPending) {
        m_downloadPending = true;
        QTimer::singleShot(3000, this, [this]() {
            m_downloadPending = false;
            downloadMissingArtistCovers();
        });
    }
}

void AlbumMosaicWidget::updateMosaic()
{
    m_coverPositions.clear();
    m_currentGridIndices.clear();

    if(m_albums.isEmpty() || m_albumOrder.isEmpty()) {
        return;
    }

    const int cols = m_columnCount;
    const int cellWidth = width() / cols;
    if(cellWidth <= 0) {
        return; // Widget not yet sized
    }
    const int cellHeight = cellWidth;
    if(cellHeight <= 0) {
        return;
    }
    const int visibleRows = (height() + cellHeight - 1) / cellHeight;
    const int totalCells = cols * visibleRows;

    // Compute grid indices deterministically from scroll offset + pre-shuffled order.
    // This way scrolling shifts the visible window by one row and covers already
    // loaded stay loaded — only the newly visible row needs fresh cover loading.
    for(int row = 0; row < visibleRows; ++row) {
        for(int col = 0; col < cols; ++col) {
            const int x = col * cellWidth;
            const int y = row * cellHeight;
            m_coverPositions.append(QRect(x, y, cellWidth, cellHeight));

            const int globalIndex = (row + m_scrollOffset) * cols + col;
            const int orderIndex = ((globalIndex % m_albumOrder.size()) + m_albumOrder.size()) % m_albumOrder.size();
            m_currentGridIndices.append(m_albumOrder[orderIndex]);
        }
    }
}

void AlbumMosaicWidget::randomizeGrid()
{
    if(m_albums.isEmpty()) {
        return;
    }

    // Build a permutation of album indices based on sort mode.
    // This stays fixed until albums change — scrolling uses updateMosaic()
    // to slide a window through this permutation.
    m_albumOrder.resize(m_albums.size());
    std::iota(m_albumOrder.begin(), m_albumOrder.end(), 0);

    switch(m_sortMode) {
        case SortMode::Random:
            std::shuffle(m_albumOrder.begin(), m_albumOrder.end(), *QRandomGenerator::global());
            break;
        case SortMode::Year:
        case SortMode::YearDesc: {
            // Use Fooyin's TrackSorter with scripting for idiomatic sorting
            if(m_trackSorter) {
                const QString sortScript = QStringLiteral("%year%");
                const auto order = (m_sortMode == SortMode::Year) ? Qt::AscendingOrder : Qt::DescendingOrder;
                m_albumOrder = m_trackSorter->calcSortTracks(
                    sortScript, m_albumOrder,
                    [this](int albumIndex) { return m_albums[albumIndex].track; }, order);
            }
            break;
        }
        case SortMode::Rating: {
            if(m_trackSorter) {
                m_albumOrder = m_trackSorter->calcSortTracks(
                    QStringLiteral("%rating%"), m_albumOrder,
                    [this](int albumIndex) { return m_albums[albumIndex].track; },
                    Qt::DescendingOrder);
            }
            break;
        }
        case SortMode::PlayCount: {
            if(m_trackSorter) {
                m_albumOrder = m_trackSorter->calcSortTracks(
                    QStringLiteral("%playcount%"), m_albumOrder,
                    [this](int albumIndex) { return m_albums[albumIndex].track; },
                    Qt::DescendingOrder);
            }
            break;
        }
        case SortMode::Recent: {
            if(m_trackSorter) {
                m_albumOrder = m_trackSorter->calcSortTracks(
                    QStringLiteral("%lastplayed%"), m_albumOrder,
                    [this](int albumIndex) { return m_albums[albumIndex].track; },
                    Qt::DescendingOrder);
            }
            break;
        }
        case SortMode::Alphabetical: {
            // Sort by album name (album mode) or artist name (artist mode)
            const QString sortScript = (m_displayMode == DisplayMode::Artist)
                ? QStringLiteral("%albumartist%")
                : QStringLiteral("%album%");
            if(m_trackSorter) {
                m_albumOrder = m_trackSorter->calcSortTracks(
                    sortScript, m_albumOrder,
                    [this](int albumIndex) { return m_albums[albumIndex].track; },
                    Qt::AscendingOrder);
            }
            break;
        }
    }

    updateMosaic();
}

AlbumMosaicWidget::AnimType AlbumMosaicWidget::effectiveAnimType() const
{
    if(m_animType != AnimType::Random) {
        return m_animType;
    }
    // Pick a random concrete type (excluding Random itself)
    const AnimType types[] = {AnimType::Flip3D, AnimType::Crossfade, AnimType::Slide, AnimType::Zoom, AnimType::PageCurl};
    return types[QRandomGenerator::global()->bounded(5)];
}

void AlbumMosaicWidget::markAlbumSwapped(int albumIndex)
{
    // Bounded FIFO: evict the oldest entry once full. The previous code let
    // this set grow unbounded in Multiple/Wave scopes — after ~10 batches it
    // excluded most of the cache and the candidate pool collapsed to <2,
    // silently starving all future animation batches.
    if(m_recentSwappedAlbums.contains(albumIndex)) {
        return;
    }
    m_recentSwappedAlbums.insert(albumIndex);
    m_recentSwappedQueue.append(albumIndex);
    while(m_recentSwappedQueue.size() > MAX_RECENT_SWAPPED) {
        m_recentSwappedAlbums.remove(m_recentSwappedQueue.takeFirst());
    }
}

QList<int> AlbumMosaicWidget::buildSwapCandidates(int maxCount, const QSet<int>& visibleAlbumIndices)
{
    // Random-sample order positions and keep those whose cover the provider
    // can already serve (scaledCache hit OR repository thumbnail ready).
    // Probing getCoverForPaint doubles as preloading: misses are tracked in
    // m_pendingPreload so coverAdded turns them into future candidates.
    QList<int> candidates;
    if(!m_coverProvider || m_coverPositions.isEmpty() || m_albumOrder.isEmpty()) {
        return candidates;
    }
    const QSize cellSize = m_coverPositions[0].size();
    const auto coverSize = Fooyin::CoverProvider::findThumbnailSize(cellSize);
    const int orderSize = static_cast<int>(m_albumOrder.size());
    const int samples = std::min(orderSize, std::max(96, maxCount * 4));
    QSet<int> seen;
    for(int s = 0; s < samples && candidates.size() < maxCount; ++s) {
        const int i = QRandomGenerator::global()->bounded(orderSize);
        if(seen.contains(i)) {
            continue;
        }
        seen.insert(i);
        const int albumIdx = m_albumOrder[i];
        if(albumIdx < 0 || albumIdx >= m_albums.size()
           || m_recentSwappedAlbums.contains(albumIdx)
           || visibleAlbumIndices.contains(albumIdx)) {
            continue;
        }
        const QPixmap px = getCoverForPaint(albumIdx, cellSize, coverSize);
        if(!px.isNull()) {
            candidates.append(i);
        } else {
            m_pendingPreload.insert(albumIdx);
            if(m_pendingPreload.size() > 60) {
                m_pendingPreload.clear();
            }
        }
    }
    return candidates;
}

void AlbumMosaicWidget::preloadCovers(int count)
{
    // Request random non-visible covers so the swap-candidate pool grows
    // beyond the currently visible cells. Async loads are tracked in
    // m_pendingPreload — coverAdded inserts the finished cover into the cache.
    if(!m_coverProvider || m_coverPositions.isEmpty() || m_albumOrder.isEmpty()) {
        return;
    }
    const QSize cellSize = m_coverPositions[0].size();
    const auto preloadSize = Fooyin::CoverProvider::findThumbnailSize(cellSize);
    for(int i = 0; i < count; ++i) {
        const int randomAlbumIdx = QRandomGenerator::global()->bounded(m_albumOrder.size());
        const int albumIdx = m_albumOrder[randomAlbumIdx];
        if(albumIdx >= 0 && albumIdx < m_albums.size() && !m_scaledCache.contains(albumIdx)
           && !m_pendingPreload.contains(albumIdx)) {
            const QPixmap px = getCoverForPaint(albumIdx, cellSize, preloadSize);
            if(px.isNull()) {
                m_pendingPreload.insert(albumIdx);
                if(m_pendingPreload.size() > 60) {
                    m_pendingPreload.clear(); // bound the set; stragglers get re-requested
                }
            }
        }
    }
}

void AlbumMosaicWidget::skipAnimationBatch(const char* reason)
{
    // A skipped batch is invisible to the user — log why and retry sooner than
    // the full animation interval so animations start as soon as covers land
    // in the cache (previously, a cold cache meant silent skips forever).
    m_consecutiveSkips++;
    if(m_consecutiveSkips <= 3 || m_consecutiveSkips % 10 == 0) {
        qDebug() << "[AlbumMosaic] animation batch skipped (x" << m_consecutiveSkips << "):" << reason
                 << "| cache:" << m_scaledCache.size()
                 << "visible:" << m_currentGridIndices.size()
                 << "pending:" << m_pendingPreload.size()
                 << "recent:" << m_recentSwappedAlbums.size();
    }
    // Starving: burst-preload now instead of waiting for the next batch —
    // resizes can shrink adaptiveMax and evict the non-visible pool, so we
    // must actively rebuild it or the retry loop starves forever.
    preloadCovers(8);
    if(!m_animRetryPending) {
        m_animRetryPending = true;
        QTimer::singleShot(1000, this, [this]() {
            m_animRetryPending = false;
            if(m_enableAnim && m_activeAnims.isEmpty()) {
                triggerAnimation();
            }
        });
    }
}

void AlbumMosaicWidget::triggerAnimation()
{
    if(m_coverPositions.isEmpty() || m_albumOrder.isEmpty() || !m_coverProvider) {
        return;
    }

    // Duration based on speed setting
    const int durationMs = [this]() {
        switch(m_animSpeed) {
            case AnimSpeed::Fast: return 250;
            case AnimSpeed::Slow: return 2000;
            default: return 800;
        }
    }();

    const auto coverSize = Fooyin::CoverProvider::findThumbnailSize(m_coverPositions[0].size());
    const int cols = m_columnCount;

    // Start new animations if none are active (timer-triggered)
    if(m_activeAnims.isEmpty()) {
        // Do NOT clear recent swaps here — we want to prevent the previous
        // batch's albums from being swapped back (which would undo the animation)
        const int numCells = m_coverPositions.size();

        // Pre-load random covers into scaledCache to expand the pool of
        // swappable albums beyond just the currently visible ones.
        // Adaptive: more preload when few visible covers (zoomed in),
        // less when many visible (zoomed out) to limit memory.
        {
            const int visibleCount = m_currentGridIndices.size();
            const int adaptiveMax = std::max(MAX_CACHE_SIZE, visibleCount * 3 / 2);
            const int headroom = adaptiveMax - m_scaledCache.size();
            preloadCovers(std::min(10, std::max(3, headroom / 3)));
        }

        // Safety net: verify m_albumOrder is still a valid permutation.
        // Past corruption (from duplicate swap targets) persists until regenerated.
        {
            QSet<int> seen;
            bool corrupted = false;
            for(int idx : m_albumOrder) {
                if(idx < 0 || idx >= m_albums.size() || seen.contains(idx)) {
                    corrupted = true;
                    break;
                }
                seen.insert(idx);
            }
            if(corrupted) {
                qWarning() << "[AlbumMosaic] m_albumOrder corrupted, regenerating";
                randomizeGrid();
                return;
            }
        }

        // Build set of currently visible album indices to avoid duplicates
        QSet<int> visibleAlbumIndices;
        for(int idx : m_currentGridIndices) {
            visibleAlbumIndices.insert(idx);
        }

        if(m_animScope == AnimScope::Single) {
            // Single cell animation — only swap with albums whose cover is ready
            ActiveAnim anim;
            anim.cellIndex = QRandomGenerator::global()->bounded(numCells);
            const int row = anim.cellIndex / cols;
            const int col = anim.cellIndex % cols;
            const int globalIndex = (row + m_scrollOffset) * cols + col;
            anim.orderIndex = ((globalIndex % m_albumOrder.size()) + m_albumOrder.size()) % m_albumOrder.size();
            anim.oldAlbumIndex = m_albumOrder[anim.orderIndex];

            QList<int> cachedAlbums = buildSwapCandidates(32, visibleAlbumIndices);
            if(cachedAlbums.isEmpty()) {
                skipAnimationBatch("single: no ready candidates");
                return;
            }

            // Pick a swap target from cached albums only
            int swapWith = cachedAlbums[QRandomGenerator::global()->bounded(cachedAlbums.size())];
            anim.newAlbumIndex = m_albumOrder[swapWith];
            anim.swapWithOrderIndex = swapWith;
            markAlbumSwapped(anim.oldAlbumIndex);
            markAlbumSwapped(anim.newAlbumIndex);

            anim.animType = effectiveAnimType();
            anim.elapsed.start();
            m_activeAnims.append(anim);
        } else if(m_animScope == AnimScope::Multiple) {
            // Multiple random cells animate simultaneously (max 5 to limit CPU/GPU)
            // Only swap with albums whose cover is ready and not visible
            QList<int> cachedAlbums = buildSwapCandidates(32, visibleAlbumIndices);
            if(cachedAlbums.size() < 2) {
                skipAnimationBatch("multiple: <2 ready candidates");
                return;
            }

            const int numToAnimate = std::min({5, numCells, static_cast<int>(cachedAlbums.size())});
            QSet<int> usedCells;
            QSet<int> usedOrders;
            for(int i = 0; i < numToAnimate; ++i) {
                ActiveAnim anim;
                int cell;
                int attempts = 0;
                do {
                    cell = QRandomGenerator::global()->bounded(numCells);
                    attempts++;
                } while(usedCells.contains(cell) && attempts < 20);
                if(usedCells.contains(cell)) continue;
                usedCells.insert(cell);

                anim.cellIndex = cell;
                anim.delayMs = i * 100; // Staggered start

                const int row = cell / cols;
                const int col = cell % cols;
                const int globalIndex = (row + m_scrollOffset) * cols + col;
                anim.orderIndex = ((globalIndex % m_albumOrder.size()) + m_albumOrder.size()) % m_albumOrder.size();
                anim.oldAlbumIndex = m_albumOrder[anim.orderIndex];

                // Pick from cached albums only
                int swapWith = cachedAlbums[QRandomGenerator::global()->bounded(cachedAlbums.size())];
                int swapAttempts = 0;
                while((swapWith == anim.orderIndex || usedOrders.contains(swapWith)) && swapAttempts < 20) {
                    swapWith = cachedAlbums[QRandomGenerator::global()->bounded(cachedAlbums.size())];
                    swapAttempts++;
                }
                if(swapWith == anim.orderIndex || usedOrders.contains(swapWith)) continue;
                usedOrders.insert(swapWith);
                anim.newAlbumIndex = m_albumOrder[swapWith];
                anim.swapWithOrderIndex = swapWith;
                markAlbumSwapped(anim.oldAlbumIndex);
                markAlbumSwapped(anim.newAlbumIndex);

                anim.animType = effectiveAnimType();
                anim.elapsed.start();
                m_activeAnims.append(anim);
            }
        } else if(m_animScope == AnimScope::Wave) {
            // Wave: animate a few cells per column with a diagonal delay
            // Only swap with albums whose cover is ready and not visible
            QList<int> cachedAlbums = buildSwapCandidates(64, visibleAlbumIndices);
            if(cachedAlbums.size() < 2) {
                skipAnimationBatch("wave: <2 ready candidates");
                return;
            }

            const int numCols = cols;
            const int visibleRows = m_coverPositions.size() / cols;
            if(visibleRows > 0 && numCols > 0) {
                const int waveTotalDelayMs = 1500;
                const int perColDelay = numCols > 1 ? waveTotalDelayMs / (numCols - 1) : 0;
                const int cellsPerCol = std::min(2, visibleRows);
                // Prevent duplicate cells and duplicate swap targets within the batch.
                // Without these, two anims can target the same m_albumOrder position
                // and permanently corrupt the permutation (duplicates + lost albums).
                QSet<int> usedCells;
                QSet<int> usedOrders;
                for(int col = 0; col < numCols; ++col) {
                    for(int i = 0; i < cellsPerCol; ++i) {
                        const int row = QRandomGenerator::global()->bounded(visibleRows);
                        const int cell = row * cols + col;
                        if(cell >= m_coverPositions.size() || usedCells.contains(cell)) continue;
                        usedCells.insert(cell);

                        ActiveAnim anim;
                        anim.cellIndex = cell;
                        anim.delayMs = col * perColDelay + (row % 3) * 50;

                        const int globalIndex = (row + m_scrollOffset) * cols + col;
                        anim.orderIndex = ((globalIndex % m_albumOrder.size()) + m_albumOrder.size()) % m_albumOrder.size();
                        anim.oldAlbumIndex = m_albumOrder[anim.orderIndex];

                        // Pick from cached albums only, no duplicate swap targets
                        int swapWith = cachedAlbums[QRandomGenerator::global()->bounded(cachedAlbums.size())];
                        int swapAttempts = 0;
                        while((swapWith == anim.orderIndex || usedOrders.contains(swapWith)) && swapAttempts < 20) {
                            swapWith = cachedAlbums[QRandomGenerator::global()->bounded(cachedAlbums.size())];
                            swapAttempts++;
                        }
                        if(swapWith == anim.orderIndex || usedOrders.contains(swapWith)) continue;
                        usedOrders.insert(swapWith);
                        anim.newAlbumIndex = m_albumOrder[swapWith];
                        anim.swapWithOrderIndex = swapWith;
                        markAlbumSwapped(anim.oldAlbumIndex);
                        markAlbumSwapped(anim.newAlbumIndex);

                        anim.animType = effectiveAnimType();
                        anim.elapsed.start();
                        m_activeAnims.append(anim);
                    }
                }
            }
        }
    }

    // A batch produced animations — reset the skip counter for the log throttle
    if(!m_activeAnims.isEmpty()) {
        m_consecutiveSkips = 0;
    }

    // Update all active animations
    for(int i = m_activeAnims.size() - 1; i >= 0; --i) {
        ActiveAnim& anim = m_activeAnims[i];
        const int elapsed = anim.elapsed.elapsed() - anim.delayMs;
        if(elapsed < 0) {
            continue; // Not started yet (staggered delay)
        }

        float t = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(durationMs));
        float progress = t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;

        // At 50%: swap m_albumOrder. Cover is guaranteed to be in scaledCache
        // because we only pick swap targets from cached albums.
        if(progress >= 0.5f && !anim.newCoverReady && anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
            // Defensive check: only swap if m_albumOrder still holds the expected
            // values. If another anim touched these positions, cancel this anim
            // entirely to avoid corrupting the permutation (permanent duplicates).
            const bool orderValid = anim.orderIndex >= 0 && anim.orderIndex < m_albumOrder.size()
                                    && anim.swapWithOrderIndex >= 0 && anim.swapWithOrderIndex < m_albumOrder.size();
            const bool dataIntact = orderValid
                                    && m_albumOrder[anim.orderIndex] == anim.oldAlbumIndex
                                    && m_albumOrder[anim.swapWithOrderIndex] == anim.newAlbumIndex;
            if(!dataIntact) {
                qDebug() << "[AlbumMosaic] anim cancelled: m_albumOrder changed mid-flight"
                         << "cell:" << anim.cellIndex << "order:" << anim.orderIndex;
                m_activeAnims.removeAt(i);
                continue;
            }
            anim.newCoverReady = true;
            m_albumOrder[anim.orderIndex] = anim.newAlbumIndex;
            m_albumOrder[anim.swapWithOrderIndex] = anim.oldAlbumIndex;
            // Rebuild m_currentGridIndices from m_albumOrder to keep all visible
            // cells in sync — otherwise the swap target cell keeps its old album
            // and creates a duplicate.
            const int numVisible = m_coverPositions.size();
            for(int cell = 0; cell < numVisible && cell < m_currentGridIndices.size(); ++cell) {
                const int row = cell / cols;
                const int col = cell % cols;
                const int globalIndex = (row + m_scrollOffset) * cols + col;
                const int orderIdx = ((globalIndex % m_albumOrder.size()) + m_albumOrder.size()) % m_albumOrder.size();
                m_currentGridIndices[cell] = m_albumOrder[orderIdx];
            }
        }

        // Remove completed animations
        if(progress >= 1.0f) {
            m_activeAnims.removeAt(i);
        }
    }

    update();

    if(!m_activeAnims.isEmpty()) {
        QTimer::singleShot(50, this, &AlbumMosaicWidget::triggerAnimation); // ~20fps (smoother with multiple instances)
    }
}

void AlbumMosaicWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();

    // Ctrl+wheel: zoom (change column count)
    if(event->modifiers() & Qt::ControlModifier) {
        if(delta > 0 && m_columnCount > 2) {
            m_columnCount--;
        }
        else if(delta < 0 && m_columnCount < 30) {
            m_columnCount++;
        }
        // Persist the new column count
        if(m_coreContext && m_coreContext->settingsManager) {
            m_coreContext->settingsManager->set(QStringLiteral("AlbumMosaic/ColumnCount"), m_columnCount);
        }
        // Cancel any active animations — their cell indices are stale after zoom
        m_activeAnims.clear();
        m_recentSwappedAlbums.clear();
        m_recentSwappedQueue.clear();
        invalidateScaledCache();
        updateMosaic();
        update();
        updateVisibleThumbnailKeys();
        event->accept();
        return;
    }

    const int scrollAmount = delta > 0 ? -1 : 1;

    m_scrollOffset += scrollAmount;

    // Cancel active animations — their orderIndex/cellIndex were computed with
    // the old scrollOffset and no longer match the visible grid.
    m_activeAnims.clear();

    updateMosaic();
    update();

    // Pin newly visible covers in CoverProvider's cache
    updateVisibleThumbnailKeys();

    event->accept();
}

void AlbumMosaicWidget::mouseMoveEvent(QMouseEvent* event)
{
    if(m_coverPositions.isEmpty() || m_currentGridIndices.isEmpty()) {
        return;
    }

    int oldHoveredIndex = m_hoveredCellIndex;
    m_hoveredCellIndex = -1;

    for(int i = 0; i < m_coverPositions.size(); ++i) {
        const QRect& cell = m_coverPositions[i];
        if(cell.contains(event->pos())) {
            m_hoveredCellIndex = i;

            if(i < m_currentGridIndices.size()) {
                int albumIndex = m_currentGridIndices[i];
                if(albumIndex < m_albums.size()) {
                    const AlbumInfo& album = m_albums[albumIndex];
                    // Use Fooyin's styled ToolTip: title = album, subtext = artist
                    if(m_toolTip) {
                        m_toolTipTimer->stop(); // Cancel any pending hide
                        m_toolTip->setContent(album.album, album.albumArtist);
                        // AlignLeft positions the tooltip above the cursor (y - height)
                        m_toolTip->setPosition(event->globalPosition().toPoint(), Qt::AlignLeft);
                        m_toolTip->show();
                    }
                }
            }
            break;
        }
    }

    if(oldHoveredIndex != m_hoveredCellIndex) {
        update();
    }

    if(m_hoveredCellIndex == -1 && m_toolTip) {
        m_toolTipTimer->start(); // Delay hiding the tooltip
    }
}

void AlbumMosaicWidget::leaveEvent(QEvent* event)
{
    Q_UNUSED(event)
    m_hoveredCellIndex = -1;
    if(m_toolTip) {
        m_toolTipTimer->start(); // Delay hiding the tooltip
    }
    update();
}

void AlbumMosaicWidget::mousePressEvent(QMouseEvent* event)
{
    Q_UNUSED(event)
}

void AlbumMosaicWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(event->button() == Qt::LeftButton) {
        for(int i = 0; i < m_coverPositions.size(); ++i) {
            if(m_coverPositions[i].contains(event->pos())) {
                if(i < m_currentGridIndices.size()) {
                    int albumIndex = m_currentGridIndices[i];
                    if(albumIndex < m_albums.size()) {
                        const AlbumInfo& album = m_albums[albumIndex];
                        playAlbum(album.album, album.albumArtist);
                    }
                }
                break;
            }
        }
    }
}

void AlbumMosaicWidget::contextMenuEvent(QContextMenuEvent* event)
{
    int rightClickedCellIndex = -1;
    for(int i = 0; i < m_coverPositions.size(); ++i) {
        if(m_coverPositions[i].contains(event->pos())) {
            rightClickedCellIndex = i;
            break;
        }
    }

    QMenu menu(this);

    if(rightClickedCellIndex != -1 && rightClickedCellIndex < m_currentGridIndices.size()) {
        int albumIndex = m_currentGridIndices[rightClickedCellIndex];
        if(albumIndex < m_albums.size()) {
            const AlbumInfo& album = m_albums[albumIndex];
            Fooyin::TrackList albumTracks = getAlbumTracks(album.album, album.albumArtist);

            // Explicit "Play" action — label depends on display mode
            const bool isArtistMode = (m_displayMode == DisplayMode::Artist);
            QAction* playAction = menu.addAction(isArtistMode ? tr("Play Artist") : tr("Play Album"));
            connect(playAction, &QAction::triggered, this, [this, album]() {
                playAlbum(album.album, album.albumArtist);
            });

            // "Play Next" — inserts album at the front of the queue
            // Playback continues in the active playlist (e.g. Default shuffle) after the queue is empty
            QAction* playNextAction = menu.addAction(tr("Play Next"));
            connect(playNextAction, &QAction::triggered, this, [this, albumTracks]() {
                if(m_coreContext && m_coreContext->playerController) {
                    m_coreContext->playerController->queueTracksNext(albumTracks);
                }
            });

            // "Play and Replace Queue" — clears the queue, then plays the album
            QAction* playReplaceQueueAction = menu.addAction(tr("Play and Replace Queue"));
            connect(playReplaceQueueAction, &QAction::triggered, this, [this, album, albumTracks]() {
                if(m_coreContext && m_coreContext->playerController) {
                    m_coreContext->playerController->clearQueue();
                    m_coreContext->playerController->queueTracksNext(albumTracks);
                }
            });

            // Use Fooyin's standard track context menu (Queue, Add to Playlist, Properties, etc.)
            if(m_guiContext && m_guiContext->trackSelection && !albumTracks.empty()) {
                // Register the selection with our WidgetContext so Fooyin's actions work
                Fooyin::TrackSelection selection;
                selection.tracks = albumTracks;
                m_guiContext->trackSelection->changeSelectedTracks(m_widgetContext, selection);

                // Add Fooyin actions at top level (Queue, Add to Playlist, Properties, etc.)
                m_guiContext->trackSelection->addTrackContextMenu(&menu, m_widgetContext);
                m_guiContext->trackSelection->addTrackPlaylistContextMenu(&menu);
                m_guiContext->trackSelection->addTrackQueueContextMenu(&menu);
            }

            // Our custom actions in submenus, clearly separated from Fooyin's
            menu.addSeparator();
            QMenu* albumMenu = menu.addMenu(isArtistMode ? tr("Artist") : tr("Album"));
            QAction* showInLibAction = albumMenu->addAction(tr("Show in Library"));
            connect(showInLibAction, &QAction::triggered, this, [this, album]() {
                showInLibrary(album.album, album.albumArtist);
            });
            if(m_guiContext && m_guiContext->trackSelection && !albumTracks.empty()) {
                QAction* searchCoverAction = albumMenu->addAction(tr("Search for Cover..."));
                connect(searchCoverAction, &QAction::triggered, this, [this, albumTracks]() {
                    emit m_guiContext->trackSelection->requestArtworkSearch(albumTracks, false);
                });
            }
            if(isArtistMode) {
                QAction* downloadCoverAction = albumMenu->addAction(tr("Download Artist Cover..."));
                connect(downloadCoverAction, &QAction::triggered, this, [this, albumArtist = album.albumArtist, track = album.track]() {
                    if(m_artistCoverDownloader) {
                        m_artistCoverDownloader->downloadSingleArtist(albumArtist, track);
                    }
                });
            }

            QMenu* viewMenu = menu.addMenu(tr("View"));
            addQuickSettings(viewMenu);

            menu.addSeparator();
            QAction* settingsAction = menu.addAction(tr("More Settings..."));
            connect(settingsAction, &QAction::triggered, this, [this]() {
                showSettingsDialog();
            });

            menu.exec(event->globalPos());
            return;
        }
    }

    // Background right-click: view settings only
    QMenu* viewMenu = menu.addMenu(tr("View"));
    addQuickSettings(viewMenu);
    menu.addSeparator();
    QAction* settingsAction = menu.addAction(tr("More Settings..."));
    connect(settingsAction, &QAction::triggered, this, [this]() {
        showSettingsDialog();
    });
    menu.exec(event->globalPos());
}

Fooyin::TrackList AlbumMosaicWidget::getAlbumTracks(const QString& album, const QString& albumArtist)
{
    // Bug 3: Use cache instead of scanning full library
    // In Artist mode, the cache key is albumArtist only (not album|artist)
    const QString key = (m_displayMode == DisplayMode::Artist) ? albumArtist : (album + "|" + albumArtist);
    if(m_albumTracksCache.contains(key)) {
        Fooyin::TrackList tracks = m_albumTracksCache[key];
        if(m_displayMode == DisplayMode::Artist) {
            // Sort by album then track number for artist mode
            std::sort(tracks.begin(), tracks.end(), [](const Fooyin::Track& a, const Fooyin::Track& b) {
                if(a.album() != b.album()) return a.album() < b.album();
                return a.trackNumber() < b.trackNumber();
            });
        } else {
            std::sort(tracks.begin(), tracks.end(), [](const Fooyin::Track& a, const Fooyin::Track& b) {
                return a.trackNumber() < b.trackNumber();
            });
        }
        return tracks;
    }
    return {};
}

void AlbumMosaicWidget::showInLibrary(const QString& album, const QString& albumArtist)
{
    // Feature: Show in library — select the album's tracks in the library view
    if(!m_guiContext || !m_guiContext->trackSelection) {
        return;
    }

    Fooyin::TrackList albumTracks = getAlbumTracks(album, albumArtist);
    if(albumTracks.empty()) {
        return;
    }

    // Change the track selection so other widgets (library, playlist) can react
    Fooyin::TrackSelection selection;
    selection.tracks = albumTracks;
    m_guiContext->trackSelection->changeSelectedTracks(m_widgetContext, selection);
}

void AlbumMosaicWidget::showSettingsDialog()
{
    // Fooyin pattern: singleton non-modal config dialog per widget instance
    openConfigDialog();
}

void AlbumMosaicWidget::addQuickSettings(QMenu* menu)
{
    if(!m_coreContext || !m_coreContext->settingsManager) {
        return;
    }

    auto* settings = m_coreContext->settingsManager;

    // Display mode toggle (Album / Artist)
    QMenu* modeMenu = menu->addMenu(tr("Display Mode"));
    auto addModeAction = [this, settings, modeMenu](const QString& label, DisplayMode mode) {
        QAction* action = modeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_displayMode == mode);
        connect(action, &QAction::triggered, this, [this, settings, mode]() {
            if(m_displayMode != mode) {
                m_displayMode = mode;
                settings->set(QStringLiteral("AlbumMosaic/DisplayMode"), displayModeKey(mode));
                loadAlbumMetadata();
                update();
            }
        });
    };
    addModeAction(tr("Album Covers"), DisplayMode::Album);
    addModeAction(tr("Artist Covers"), DisplayMode::Artist);

    menu->addSeparator();

    // Animation toggle
    QAction* animToggle = menu->addAction(tr("Animation"));
    animToggle->setCheckable(true);
    animToggle->setChecked(m_enableAnim);
    connect(animToggle, &QAction::triggered, this, [this, settings](bool checked) {
        m_enableAnim = checked;
        settings->set(QStringLiteral("AlbumMosaic/EnableAnim"), checked);
        if(checked) {
            m_animTimer->start(animIntervalMs());
        } else {
            m_animTimer->stop();
            m_activeAnims.clear();
            update();
        }
    });

    // Animation type submenu
    QMenu* animTypeMenu = menu->addMenu(tr("Animation Type"));
    auto addAnimTypeAction = [this, settings, animTypeMenu](const QString& label, AnimType type) {
        QAction* action = animTypeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_animType == type);
        connect(action, &QAction::triggered, this, [this, settings, type]() {
            m_animType = type;
            settings->set(QStringLiteral("AlbumMosaic/AnimType"), animTypeKey(type));
        });
    };
    addAnimTypeAction(tr("3D Flip"), AnimType::Flip3D);
    addAnimTypeAction(tr("Crossfade"), AnimType::Crossfade);
    addAnimTypeAction(tr("Slide"), AnimType::Slide);
    addAnimTypeAction(tr("Zoom"), AnimType::Zoom);
    addAnimTypeAction(tr("Page Curl"), AnimType::PageCurl);
    addAnimTypeAction(tr("Random"), AnimType::Random);

    // Animation speed submenu
    QMenu* speedMenu = menu->addMenu(tr("Animation Speed"));
    auto addSpeedAction = [this, settings, speedMenu](const QString& label, AnimSpeed speed) {
        QAction* action = speedMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_animSpeed == speed);
        connect(action, &QAction::triggered, this, [this, settings, speed]() {
            m_animSpeed = speed;
            settings->set(QStringLiteral("AlbumMosaic/AnimSpeed"), animSpeedKey(speed));
            // Restart timer with new interval (speed + scope)
            if(m_animTimer->isActive()) {
                m_animTimer->start(animIntervalMs());
            }
        });
    };
    addSpeedAction(tr("Fast"), AnimSpeed::Fast);
    addSpeedAction(tr("Medium"), AnimSpeed::Medium);
    addSpeedAction(tr("Slow"), AnimSpeed::Slow);

    // Animation scope submenu
    QMenu* scopeMenu = menu->addMenu(tr("Animation Scope"));
    auto addScopeAction = [this, settings, scopeMenu](const QString& label, AnimScope scope) {
        QAction* action = scopeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_animScope == scope);
        connect(action, &QAction::triggered, this, [this, settings, scope]() {
            m_animScope = scope;
            settings->set(QStringLiteral("AlbumMosaic/AnimScope"), animScopeKey(scope));
            // Restart timer with new interval (speed + scope)
            if(m_animTimer->isActive()) {
                m_animTimer->start(animIntervalMs());
            }
        });
    };
    addScopeAction(tr("Single"), AnimScope::Single);
    addScopeAction(tr("Multiple"), AnimScope::Multiple);
    addScopeAction(tr("Wave"), AnimScope::Wave);

    // Sort mode submenu
    QMenu* sortMenu = menu->addMenu(tr("Sort By"));
    auto addSortAction = [this, settings, sortMenu](const QString& label, SortMode mode) {
        QAction* action = sortMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(m_sortMode == mode);
        connect(action, &QAction::triggered, this, [this, settings, mode]() {
            m_sortMode = mode;
            settings->set(QStringLiteral("AlbumMosaic/SortMode"), sortModeKey(mode));
            randomizeGrid();
            invalidateScaledCache();
            update();
        });
    };
    addSortAction(tr("Random"), SortMode::Random);
    addSortAction(tr("Alphabetical"), SortMode::Alphabetical);
    addSortAction(tr("Year (newest first)"), SortMode::YearDesc);
    addSortAction(tr("Year (oldest first)"), SortMode::Year);
    addSortAction(tr("Rating (highest first)"), SortMode::Rating);
    addSortAction(tr("Play Count"), SortMode::PlayCount);
    addSortAction(tr("Recently Played"), SortMode::Recent);
}

void AlbumMosaicWidget::loadSettings()
{
    if(m_coreContext && m_coreContext->settingsManager) {
        auto* settings = m_coreContext->settingsManager;
        m_enableAnim = settings->value(QStringLiteral("AlbumMosaic/EnableAnim")).toBool();
        m_columnCount = settings->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt();
        m_genreFilter = settings->value(QStringLiteral("AlbumMosaic/GenreFilter")).toString();
        m_artistFilter = settings->value(QStringLiteral("AlbumMosaic/ArtistFilter")).toString();
        m_animType = animTypeFromKey(settings->value(QStringLiteral("AlbumMosaic/AnimType")).toString());
        m_animSpeed = animSpeedFromKey(settings->value(QStringLiteral("AlbumMosaic/AnimSpeed")).toString());
        m_animScope = animScopeFromKey(settings->value(QStringLiteral("AlbumMosaic/AnimScope")).toString());
        m_sortMode = sortModeFromKey(settings->value(QStringLiteral("AlbumMosaic/SortMode")).toString());
        m_bgColor = QColor(settings->value(QStringLiteral("AlbumMosaic/BgColor")).toString());
        if(!m_bgColor.isValid()) m_bgColor = Qt::black;
        m_displayMode = displayModeFromKey(settings->value(QStringLiteral("AlbumMosaic/DisplayMode")).toString());
        m_autoDownloadArtistCovers = settings->value(QStringLiteral("AlbumMosaic/AutoDownloadArtistCovers")).toBool();

        // Sync inline sort combo
        if(m_sortCombo) {
            int idx = m_sortCombo->findData(sortModeKey(m_sortMode));
            if(idx >= 0 && idx != m_sortCombo->currentIndex()) {
                QSignalBlocker blocker(m_sortCombo);
                m_sortCombo->setCurrentIndex(idx);
            }
        }

        if(m_enableAnim) {
            m_animTimer->start(animIntervalMs());
        } else {
            m_animTimer->stop();
        }

        // loadAlbumMetadata() will call randomizeGrid() and updateMosaic()
        loadAlbumMetadata();
        update();
    }
}

void AlbumMosaicWidget::onSortChanged()
{
    if(!m_sortCombo || !m_coreContext || !m_coreContext->settingsManager) {
        return;
    }

    const QString sortStr = m_sortCombo->currentData().toString();
    m_coreContext->settingsManager->set(QStringLiteral("AlbumMosaic/SortMode"), sortStr);

    // Update sort mode and re-sort
    m_sortMode = sortModeFromKey(sortStr);

    randomizeGrid();
    update();
}

void AlbumMosaicWidget::playAlbum(const QString& album, const QString& albumArtist)
{
    Fooyin::TrackList albumTracks = getAlbumTracks(album, albumArtist);

    if(albumTracks.empty()) {
        return;
    }

    // Bug 1: Use PlaylistHandler to create/replace a playlist and start playback properly
    if(m_coreContext && m_coreContext->playlistHandler && m_coreContext->playerController) {
        const QString playlistName = album + " - " + albumArtist;
        Fooyin::Playlist* playlist = m_coreContext->playlistHandler->createPlaylist(playlistName, albumTracks);
        if(playlist) {
            m_coreContext->playlistHandler->changeActivePlaylist(playlist);
            m_coreContext->playerController->startPlayback(playlist);
        }
    }
}

void AlbumMosaicWidget::searchEvent(const Fooyin::SearchRequest& request)
{
    // Feature: Search integration — debounce so each keystroke doesn't rescan
    // the entire library; reload once the user pauses typing.
    m_searchQuery = request.text;
    if(!m_searchTimer) {
        m_searchTimer = new QTimer(this);
        m_searchTimer->setSingleShot(true);
        m_searchTimer->setInterval(300);
        connect(m_searchTimer, &QTimer::timeout, this, [this]() {
            loadAlbumMetadata();
            update();
        });
    }
    m_searchTimer->start();
}

void AlbumMosaicWidget::saveLayoutData(QJsonObject& layout)
{
    // Per-instance config — each widget instance keeps its own settings in the
    // layout (Fooyin's per-instance config model, like CoverWidget::ConfigData).
    // Global SettingsManager values remain the defaults for new instances.
    layout[QStringLiteral("scrollOffset")] = m_scrollOffset;
    layout[QStringLiteral("columnCount")] = m_columnCount;
    layout[QStringLiteral("enableAnim")] = m_enableAnim;
    layout[QStringLiteral("animType")] = animTypeKey(m_animType);
    layout[QStringLiteral("animSpeed")] = animSpeedKey(m_animSpeed);
    layout[QStringLiteral("animScope")] = animScopeKey(m_animScope);
    layout[QStringLiteral("sortMode")] = sortModeKey(m_sortMode);
    layout[QStringLiteral("bgColor")] = m_bgColor.name();
    layout[QStringLiteral("genreFilter")] = m_genreFilter;
    layout[QStringLiteral("artistFilter")] = m_artistFilter;
    layout[QStringLiteral("displayMode")] = displayModeKey(m_displayMode);
}

void AlbumMosaicWidget::loadLayoutData(const QJsonObject& layout)
{
    // Restore per-instance config — overrides the global defaults loaded in the
    // constructor so two instances can run different settings.
    if(layout.contains(QStringLiteral("scrollOffset"))) {
        m_scrollOffset = layout.value(QStringLiteral("scrollOffset")).toInt();
    }
    if(layout.contains(QStringLiteral("columnCount"))) {
        m_columnCount = layout.value(QStringLiteral("columnCount")).toInt();
    }
    if(layout.contains(QStringLiteral("enableAnim"))) {
        m_enableAnim = layout.value(QStringLiteral("enableAnim")).toBool();
    }
    if(layout.contains(QStringLiteral("animType"))) {
        m_animType = animTypeFromKey(layout.value(QStringLiteral("animType")).toString());
    }
    if(layout.contains(QStringLiteral("animSpeed"))) {
        m_animSpeed = animSpeedFromKey(layout.value(QStringLiteral("animSpeed")).toString());
    }
    if(layout.contains(QStringLiteral("animScope"))) {
        m_animScope = animScopeFromKey(layout.value(QStringLiteral("animScope")).toString());
    }
    if(layout.contains(QStringLiteral("sortMode"))) {
        m_sortMode = sortModeFromKey(layout.value(QStringLiteral("sortMode")).toString());
    }
    if(layout.contains(QStringLiteral("bgColor"))) {
        const QColor c(layout.value(QStringLiteral("bgColor")).toString());
        if(c.isValid()) m_bgColor = c;
    }
    if(layout.contains(QStringLiteral("genreFilter"))) {
        m_genreFilter = layout.value(QStringLiteral("genreFilter")).toString();
    }
    if(layout.contains(QStringLiteral("artistFilter"))) {
        m_artistFilter = layout.value(QStringLiteral("artistFilter")).toString();
    }
    if(layout.contains(QStringLiteral("displayMode"))) {
        m_displayMode = displayModeFromKey(layout.value(QStringLiteral("displayMode")).toString());
    }
}

void AlbumMosaicWidget::finalise()
{
    // Called after layout data is restored — restart the animation timer with
    // the restored per-instance config (the constructor may have used defaults).
    if(m_enableAnim) {
        m_animTimer->start(animIntervalMs());
    } else {
        m_animTimer->stop();
    }
}

void AlbumMosaicWidget::openConfigDialog()
{
    if(!m_coreContext || !m_coreContext->settingsManager) {
        return;
    }
    auto* dialog = new AlbumMosaicSettingsDialog(m_coreContext->settingsManager, m_coreContext->library, this);
    connect(dialog, &QDialog::accepted, this, &AlbumMosaicWidget::loadSettings);
    // Fooyin keeps the dialog singleton per widget instance
    showConfigDialog(dialog);
}

void AlbumMosaicWidget::layoutEditingMenu(QMenu* menu)
{
    // Expose a "Configure…" action in Fooyin's layout-editing context menu
    addConfigureAction(menu);
}

void AlbumMosaicWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)

    QPainter painter(this);
    painter.fillRect(rect(), m_bgColor);

    if(m_albums.isEmpty() || m_coverPositions.isEmpty()) {
        return;
    }

    // Feature: Adaptive cover size based on cell size
    const auto coverSize = Fooyin::CoverProvider::findThumbnailSize(m_coverPositions[0].size());
    const bool useAdaptiveSize = m_coverProvider != nullptr;

    // Duration based on speed setting (must match triggerAnimation)
    const int durationMs = [this]() {
        switch(m_animSpeed) {
            case AnimSpeed::Fast: return 250;
            case AnimSpeed::Slow: return 2000;
            default: return 800;
        }
    }();

    for(int i = 0; i < m_coverPositions.size(); ++i) {
        const QRect& cell = m_coverPositions[i];
        const bool isHovered = (i == m_hoveredCellIndex);

        // Find if this cell has an active animation
        int animIdx = -1;
        float flipProgress = 0.0f;
        for(int a = 0; a < m_activeAnims.size(); ++a) {
            if(m_activeAnims[a].cellIndex == i) {
                const int elapsed = m_activeAnims[a].elapsed.elapsed() - m_activeAnims[a].delayMs;
                if(elapsed >= 0) {
                    float t = std::min(1.0f, static_cast<float>(elapsed) / static_cast<float>(durationMs));
                    flipProgress = t < 0.5f ? 4.0f * t * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) / 2.0f;
                    animIdx = a;
                    break;
                }
            }
        }
        const bool isAnimating = (animIdx >= 0);

        if(i < m_currentGridIndices.size()) {
            int albumIndex = m_currentGridIndices[i];
            if(albumIndex < m_albums.size()) {
                const AlbumInfo& album = m_albums[albumIndex];

                // Feature: Highlight currently playing album (pre-resolved index)
                const bool isPlaying = (albumIndex == m_playingAlbumIndex);

                // Use cached scaled cover — avoids loading + scaling every frame
                QPixmap scaledCover = getCoverForPaint(albumIndex, cell.size(), coverSize);

                // Always draw placeholder first (covers fade in over it)
                {
                    QLinearGradient gradient(cell.topLeft(), cell.bottomRight());
                    gradient.setColorAt(0, QColor(60, 60, 70));
                    gradient.setColorAt(1, QColor(40, 40, 50));
                    painter.fillRect(cell, gradient);
                }

                if(!scaledCover.isNull()) {
                    // Get fade progress (0-100), default to 100 if not fading
                    int fade = m_coverFadeProgress.value(albumIndex, 100);
                    qreal opacity = fade / 100.0;

                    // Fit the pixmap into the cell — handles stale-size cache
                    // entries during a resize drag (stretched until cache refreshes)
                    QRect destRect(QPoint(0, 0), scaledCover.size().scaled(cell.size(), Qt::KeepAspectRatio));
                    destRect.moveCenter(cell.center());

                    if(isAnimating) {
                        const ActiveAnim& anim = m_activeAnims[animIdx];
                        painter.save();

                        // Determine which album to show based on animation progress and cover readiness
                        const bool showNew = (flipProgress >= 0.5f && anim.newCoverReady);
                        int albumToShow = showNew ? anim.newAlbumIndex : anim.oldAlbumIndex;

                        QPixmap animCover = scaledCover;
                        QRect animRect = destRect;
                        if(albumToShow >= 0 && albumToShow < m_albums.size() && albumToShow != albumIndex) {
                            animCover = getCoverForPaint(albumToShow, cell.size(), coverSize);
                            if(!animCover.isNull()) {
                                animRect = QRect(QPoint(0, 0), animCover.size().scaled(cell.size(), Qt::KeepAspectRatio));
                                animRect.moveCenter(cell.center());
                            } else {
                                animCover = scaledCover;
                            }
                        }

                        switch(anim.animType) {
                            case AnimType::Flip3D: {
                                float angle = flipProgress * 180.0f;
                                float scaleX = std::cos(angle * M_PI / 180.0f);
                                const float absScale = std::abs(scaleX);

                                // Shadow
                                const float shadowAlpha = (1.0f - absScale) * 120.0f;
                                if(shadowAlpha > 10.0f) {
                                    QRect shadowRect = animRect.adjusted(4, 6, 4, 6);
                                    painter.setPen(Qt::NoPen);
                                    painter.setBrush(QColor(0, 0, 0, static_cast<int>(shadowAlpha)));
                                    painter.drawRoundedRect(shadowRect, 4, 4);
                                }

                                // Perspective transform
                                const float verticalLift = (1.0f - absScale) * 0.08f;
                                const float shear = (1.0f - absScale) * 0.03f * (scaleX > 0 ? 1.0f : -1.0f);
                                painter.translate(animRect.center());
                                painter.scale(absScale, 1.0f + verticalLift);
                                if(std::abs(shear) > 0.001f) painter.shear(shear, 0);
                                painter.translate(-animRect.center());

                                // Dim for depth
                                painter.setOpacity(0.5f + 0.5f * absScale);
                                painter.drawPixmap(animRect, animCover);
                                painter.setOpacity(1.0);

                                // Edge highlight
                                if(absScale < 0.15f) {
                                    const float edgeAlpha = (1.0f - absScale / 0.15f) * 200.0f;
                                    painter.setPen(QPen(QColor(220, 220, 255, static_cast<int>(edgeAlpha)), 2));
                                    painter.drawLine(animRect.topLeft(), animRect.bottomLeft());
                                    painter.drawLine(animRect.topRight(), animRect.bottomRight());
                                }
                                break;
                            }

                            case AnimType::Crossfade: {
                                // Draw new cover at full opacity as the base layer (only if ready),
                                // then overlay old cover with decreasing opacity on top.
                                // This avoids showing the black background during the transition.
                                if(showNew && albumToShow != albumIndex) {
                                    painter.drawPixmap(animRect, animCover);
                                    painter.setOpacity(1.0f - flipProgress);
                                    painter.drawPixmap(animRect, scaledCover);
                                } else {
                                    // New cover not ready — just fade old cover slightly
                                    painter.setOpacity(1.0f - flipProgress * 0.3f);
                                    painter.drawPixmap(animRect, scaledCover);
                                }
                                painter.setOpacity(1.0f);
                                break;
                            }

                            case AnimType::Slide: {
                                // Old cover slides up and out, new cover slides up from bottom
                                const int slideOffset = static_cast<int>(cell.height() * flipProgress);
                                if(!showNew || albumToShow == albumIndex) {
                                    // Still showing old — slide up
                                    QRect slideRect = animRect.translated(0, -slideOffset);
                                    painter.setOpacity(1.0f - flipProgress);
                                    painter.drawPixmap(slideRect, scaledCover);
                                } else {
                                    // Old slides up and out
                                    QRect oldRect = animRect.translated(0, -slideOffset);
                                    painter.setOpacity(1.0f - flipProgress);
                                    painter.drawPixmap(oldRect, scaledCover);

                                    // New slides up from bottom
                                    QRect newRect = animRect.translated(0, cell.height() - slideOffset);
                                    painter.setOpacity(flipProgress);
                                    painter.drawPixmap(newRect, animCover);
                                }
                                painter.setOpacity(1.0f);
                                break;
                            }

                            case AnimType::Zoom: {
                                // Old cover zooms out and fades, new cover zooms in and fades in
                                const float oldScale = 1.0f - flipProgress * 0.5f;
                                const float newScale = 0.5f + flipProgress * 0.5f;

                                // Draw old (zooming out)
                                painter.setOpacity(1.0f - flipProgress);
                                painter.translate(animRect.center());
                                painter.scale(oldScale, oldScale);
                                painter.translate(-animRect.center());
                                painter.drawPixmap(animRect, scaledCover);
                                painter.restore();
                                painter.save();

                                // Draw new (zooming in) if ready
                                if(showNew && albumToShow != albumIndex) {
                                    painter.setOpacity(flipProgress);
                                    painter.translate(animRect.center());
                                    painter.scale(newScale, newScale);
                                    painter.translate(-animRect.center());
                                    painter.drawPixmap(animRect, animCover);
                                }
                                painter.setOpacity(1.0f);
                                break;
                            }

                            case AnimType::PageCurl: {
                                // Simulate page curl: clip reveals new cover progressively
                                // from the right edge, old cover curls away
                                const float curlWidth = flipProgress * animRect.width();

                                // Draw old cover with curl shadow
                                painter.drawPixmap(animRect, scaledCover);

                                // Darken the curling part
                                if(flipProgress > 0.01f && flipProgress < 0.99f) {
                                    QRect curlRect = animRect;
                                    curlRect.setLeft(animRect.right() - static_cast<int>(curlWidth));
                                    QLinearGradient curlGrad(curlRect.topLeft(), curlRect.topRight());
                                    curlGrad.setColorAt(0, QColor(0, 0, 0, 0));
                                    curlGrad.setColorAt(1, QColor(0, 0, 0, static_cast<int>(120 * flipProgress)));
                                    painter.fillRect(curlRect, curlGrad);
                                }

                                // Draw new cover revealed from left
                                if(showNew && albumToShow != albumIndex) {
                                    painter.save();
                                    QRect clipRect = animRect;
                                    clipRect.setWidth(static_cast<int>(animRect.width() * flipProgress));
                                    painter.setClipRect(clipRect);
                                    painter.drawPixmap(animRect, animCover);
                                    painter.restore();

                                    // Curl edge shadow on new cover
                                    if(flipProgress > 0.01f && flipProgress < 0.99f) {
                                        QRect edgeRect = animRect;
                                        edgeRect.setLeft(static_cast<int>(animRect.width() * flipProgress) - 3);
                                        edgeRect.setWidth(6);
                                        QLinearGradient edgeGrad(edgeRect.topLeft(), edgeRect.topRight());
                                        edgeGrad.setColorAt(0, QColor(0, 0, 0, 80));
                                        edgeGrad.setColorAt(0.5, QColor(0, 0, 0, 40));
                                        edgeGrad.setColorAt(1, QColor(0, 0, 0, 0));
                                        painter.fillRect(edgeRect, edgeGrad);
                                    }
                                }
                                break;
                            }
                        }

                        if(isHovered) {
                            painter.setPen(QPen(QColor(255, 255, 255, 100), 3));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawRect(destRect);
                        }

                        painter.restore();
                    } else {
                        painter.save();
                        painter.setOpacity(opacity);
                        if(isHovered) {
                            painter.setPen(QPen(QColor(255, 255, 255, 100), 3));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawRect(destRect);
                            painter.drawPixmap(destRect, scaledCover);
                        } else {
                            painter.drawPixmap(destRect, scaledCover);
                        }
                        painter.restore();
                    }

                    // Feature: Highlight currently playing album with green border
                    if(isPlaying) {
                        painter.setPen(QPen(QColor(0, 200, 0, 200), 4));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(cell);
                    }

                    if(isHovered) {
                        painter.setPen(QColor(255, 255, 255));
                        QFont font = painter.font();
                        font.setPixelSize(cell.height() / 10);
                        font.setBold(true);
                        painter.setFont(font);

                        QString shortName = album.album;
                        if(shortName.length() > 20) {
                            shortName = shortName.left(17) + "...";
                        }

                        QRect textRect = destRect;
                        textRect.setHeight(cell.height() / 5);
                        textRect.moveBottom(destRect.bottom() - 5);

                        painter.fillRect(textRect, QColor(0, 0, 0, 150));
                        painter.drawText(textRect, Qt::AlignCenter, shortName);
                    }
                } else {
                    // No cover — draw placeholder
                    if(m_displayMode == DisplayMode::Artist) {
                        // Artist mode: colored tile with artist initial (distinct per artist)
                        const QString name = album.albumArtist;
                        const QString initial = name.isEmpty() ? "?" : QString(name.at(0)).toUpper();

                        // Generate a consistent color from the artist name hash
                        const uint hash = qHash(name);
                        const int hue = hash % 360;
                        const QColor bgColor = QColor::fromHsl(hue, 140, 80); // medium saturation, dark
                        const QColor fgColor = QColor::fromHsl(hue, 60, 200); // light text

                        painter.fillRect(cell, bgColor);

                        // Draw large initial letter
                        painter.setPen(fgColor);
                        QFont font = painter.font();
                        font.setPixelSize(cell.height() / 2);
                        font.setBold(true);
                        painter.setFont(font);
                        painter.drawText(cell, Qt::AlignCenter, initial);

                        // Artist name at bottom
                        font.setPixelSize(cell.height() / 10);
                        font.setBold(false);
                        painter.setFont(font);
                        painter.setPen(QColor(255, 255, 255, 180));
                        QString shortName = name;
                        if(shortName.length() > 20) {
                            shortName = shortName.left(17) + "...";
                        }
                        QRect textRect = cell;
                        textRect.setTop(cell.top() + cell.height() * 3 / 4);
                        painter.drawText(textRect, Qt::AlignCenter, shortName);
                    } else {
                        // Album mode: generic placeholder (♪ + album name)
                        painter.setPen(QColor(200, 200, 200));
                        QFont font = painter.font();
                        font.setPixelSize(cell.height() / 3);
                        painter.setFont(font);
                        painter.drawText(cell, Qt::AlignCenter, "♪");

                        font.setPixelSize(cell.height() / 8);
                        painter.setFont(font);
                        QString shortName = album.album;
                        if(shortName.length() > 15) {
                            shortName = shortName.left(12) + "...";
                        }
                        QRect textRect = cell;
                        textRect.setTop(cell.top() + cell.height() / 2);
                        painter.drawText(textRect, Qt::AlignCenter, shortName);
                    }

                    // Common overlays for placeholder cells
                    if(isHovered) {
                        painter.setPen(QPen(QColor(255, 255, 255, 100), 3));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(cell);
                    }

                    if(isPlaying) {
                        painter.setPen(QPen(QColor(0, 200, 0, 200), 4));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(cell);
                    }
                }
            }
        }
    }

    // Cover loading is handled by CoverProvider's async scan + coverAdded signal.
    // No need to schedule repaints here.
}

void AlbumMosaicWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    // Position sort combo in top-right corner
    if(m_sortCombo) {
        m_sortCombo->move(width() - m_sortCombo->width() - 8, 8);
    }
    // Relayout immediately (cheap index math) so cells track the resize.
    // Covers are drawn stretched during the drag — see getCoverForPaint.
    updateMosaic();
    update();

    // Debounce the expensive work: invalidating the scaled cache triggers a
    // re-scale of every visible cover. During a drag-resize this fires dozens
    // of times per second — coalesce until the resize settles.
    if(!m_resizeTimer) {
        m_resizeTimer = new QTimer(this);
        m_resizeTimer->setSingleShot(true);
        m_resizeTimer->setInterval(80);
        connect(m_resizeTimer, &QTimer::timeout, this, [this]() {
            invalidateScaledCache();
            update();
            updateVisibleThumbnailKeys();
        });
    }
    m_resizeTimer->start();
}

void AlbumMosaicWidget::invalidateScaledCache()
{
    m_scaledCache.clear();
    m_scaledCacheCellSize = QSize(0, 0);
}

QPixmap AlbumMosaicWidget::getCoverForPaint(int albumIndex, const QSize& cellSize, const Fooyin::ThumbnailSize& coverSize)
{
    // Return scaled cache if available — even at a stale size. During a resize
    // drag the cell size changes every frame; re-scaling every cover each frame
    // would stall the UI. The paint path stretches stale pixmaps instead, and
    // the debounced resize timer invalidates the cache once the drag settles.
    if(m_scaledCache.contains(albumIndex)) {
        return m_scaledCache[albumIndex];
    }

    // Not in scaled cache — query CoverProvider (cheap cache lookup, triggers async load if not ready)
    if(!m_coverProvider || albumIndex < 0 || albumIndex >= m_albums.size()) {
        return {};
    }

    const AlbumInfo& album = m_albums[albumIndex];
    if(!album.track.isValid()) {
        return {};
    }

    QPixmap cover = m_coverProvider->trackCoverThumbnail(album.track, coverSize, coverType());

    // Check if CoverProvider returned the placeholder (cover not yet loaded)
    if(cover.isNull() || cover.cacheKey() == m_placeholderCacheKey) {
        // Fallback for Artist mode: the downloader saves covers to a plugin
        // cache keyed by md5(artist) so the plugin always finds them regardless
        // of which track is representative. Also check the artist's parent dir
        // and the track's own dir (for covers attached via fooyin's dialog).
        if(m_displayMode == DisplayMode::Artist) {
            const QString cacheDir = QDir::homePath() + "/.local/share/fooyin/artistcovers";
            const QString md5 = QString(QCryptographicHash::hash(album.albumArtist.toUtf8(), QCryptographicHash::Md5).toHex());
            const QString trackDir = QFileInfo(album.track.filepath()).absolutePath();
            const QString parentDir = QDir(trackDir).absoluteFilePath("..");
            const QDir artistDir{parentDir};

            // Search order: plugin cache > artist parent dir > track dir (fooyin attach)
            const QStringList candidates = {
                cacheDir + "/" + md5 + ".jpg",
                cacheDir + "/" + md5 + ".png",
                artistDir.absoluteFilePath("artist.jpg"),
                artistDir.absoluteFilePath("artist.png"),
                QDir(trackDir).absoluteFilePath("artist.jpg"),
                QDir(trackDir).absoluteFilePath("artist.png"),
                QDir(trackDir).absoluteFilePath("artist.jpeg"),
            };
            for(const QString& path : candidates) {
                if(QFile::exists(path) && cover.load(path)) {
                    break;
                }
            }
            if(cover.isNull() || cover.cacheKey() == m_placeholderCacheKey) {
                return {}; // coverAdded will fire when the real cover is available
            }
        } else {
            return {}; // coverAdded will fire when the real cover is available
        }
    }

    // Scale to cell size and cache
    // Adaptive cache limit: visible covers + 50% headroom for preloaded swap candidates
    const int visibleCount = m_currentGridIndices.size();
    const int adaptiveMax = std::max(MAX_CACHE_SIZE, visibleCount * 3 / 2);
    while(m_scaledCache.size() >= adaptiveMax) {
        // Build set of currently visible album indices
        QSet<int> visibleIndices;
        for(int idx : m_currentGridIndices) {
            visibleIndices.insert(idx);
        }
        // Find first non-visible entry to evict
        int toRemove = -1;
        for(auto it = m_scaledCache.begin(); it != m_scaledCache.end(); ++it) {
            if(!visibleIndices.contains(it.key())) {
                toRemove = it.key();
                break;
            }
        }
        // If all are visible, evict the oldest (begin)
        if(toRemove == -1) {
            toRemove = m_scaledCache.begin().key();
        }
        m_scaledCache.remove(toRemove);
    }

    // Scale at device pixels for HiDPI sharpness (CoverWidget does the same).
    // A dpr=2 pixmap drawn into a logical-size rect renders at full resolution.
    const double dpr = devicePixelRatioF();
    QPixmap scaled = cover.scaled(cellSize * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    scaled.setDevicePixelRatio(dpr);
    m_scaledCache[albumIndex] = scaled;
    m_scaledCacheCellSize = cellSize;
    return scaled;
}

void AlbumMosaicWidget::updateVisibleThumbnailKeys()
{
    if(!m_coverProvider || m_coverPositions.isEmpty()) {
        return;
    }

    const auto coverSize = Fooyin::CoverProvider::findThumbnailSize(m_coverPositions[0].size());
    std::set<QString> keys;

    for(int albumIndex : m_currentGridIndices) {
        if(albumIndex < 0 || albumIndex >= m_albums.size()) continue;
        const AlbumInfo& album = m_albums[albumIndex];
        if(!album.track.isValid()) continue;
        keys.insert(m_coverProvider->thumbnailCacheKey(album.track, coverSize, coverType()));
    }

    m_coverProvider->setVisibleThumbnailKeys(this, keys);
}

void AlbumMosaicWidget::advanceFade()
{
    bool anyFading = false;
    for(int albumIndex : m_currentGridIndices) {
        if(!m_coverFadeProgress.contains(albumIndex)) continue;
        int progress = m_coverFadeProgress[albumIndex];
        if(progress < 100) {
            progress = qMin(100, progress + 100 / FADE_STEPS);
            m_coverFadeProgress[albumIndex] = progress;
            anyFading = true;
        }
    }

    if(anyFading) {
        update();
    } else {
        m_fadeTimer->stop();
    }
}

Fooyin::Track::Cover AlbumMosaicWidget::coverType() const
{
    return (m_displayMode == DisplayMode::Artist) ? Fooyin::Track::Cover::Artist : Fooyin::Track::Cover::Front;
}

void AlbumMosaicWidget::downloadMissingArtistCovers()
{
    if(m_displayMode != DisplayMode::Artist || m_albums.isEmpty() || !m_artistCoverDownloader) {
        return;
    }

    // Build list of artists with their representative tracks
    QList<QPair<QString, Fooyin::Track>> artists;
    for(const AlbumInfo& info : m_albums) {
        if(info.track.isValid()) {
            artists.append({info.albumArtist, info.track});
        }
    }

    m_artistCoverDownloader->downloadMissing(artists);
}

void AlbumMosaicWidget::onArtistCoverDownloaded(const QString& artist)
{
    // Invalidate the scaled cache for this artist so the new cover loads
    const int artistIndex = m_albumKeyToIndex.value(artist, -1);
    if(artistIndex >= 0) {
        m_scaledCache.remove(artistIndex);
        // Also invalidate the CoverProvider cache for the representative track
        if(m_coverProvider && m_albums[artistIndex].track.isValid()) {
            m_coverProvider->removeFromCache(m_albums[artistIndex].track);
        }
        // Trigger a repaint to show the new cover
        if(m_currentGridIndices.contains(artistIndex)) {
            m_coverFadeProgress[artistIndex] = 0;
            if(m_fadeTimer && !m_fadeTimer->isActive()) {
                m_fadeTimer->start();
            }
        }
        update();
    }
}
