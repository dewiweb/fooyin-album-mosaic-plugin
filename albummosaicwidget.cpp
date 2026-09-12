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

AlbumMosaicWidget::AlbumMosaicWidget(Fooyin::GuiPluginContext* guiContext, Fooyin::CorePluginContext* coreContext, Fooyin::CoverProvider* coverProvider, QWidget* parent)
    : FyWidget{parent}
    , m_guiContext{guiContext}
    , m_coreContext{coreContext}
    , m_coverProvider{coverProvider}
    , m_animTimer{new QTimer(this)}
{
    if(m_coreContext && m_coreContext->settingsManager) {
        m_enableAnim = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/EnableAnim")).toBool();
        m_animInterval = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimInterval")).toInt();
        m_columnCount = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt();
        m_genreFilter = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/GenreFilter")).toString();
        m_artistFilter = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/ArtistFilter")).toString();

        // Load animation type
        const QString animTypeStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimType")).toString();
        if(animTypeStr == "Crossfade") m_animType = AnimType::Crossfade;
        else if(animTypeStr == "Slide") m_animType = AnimType::Slide;
        else if(animTypeStr == "Zoom") m_animType = AnimType::Zoom;
        else if(animTypeStr == "PageCurl") m_animType = AnimType::PageCurl;
        else if(animTypeStr == "Random") m_animType = AnimType::Random;
        else m_animType = AnimType::Flip3D;

        // Load animation speed
        const QString speedStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimSpeed")).toString();
        if(speedStr == "Fast") m_animSpeed = AnimSpeed::Fast;
        else if(speedStr == "Slow") m_animSpeed = AnimSpeed::Slow;
        else m_animSpeed = AnimSpeed::Medium;

        // Load animation scope
        const QString scopeStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimScope")).toString();
        if(scopeStr == "Multiple") m_animScope = AnimScope::Multiple;
        else if(scopeStr == "Wave") m_animScope = AnimScope::Wave;
        else m_animScope = AnimScope::Single;

        // Load background color
        m_bgColor = QColor(m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/BgColor")).toString());
        if(!m_bgColor.isValid()) m_bgColor = Qt::black;

        // Load sort mode
        const QString sortStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/SortMode")).toString();
        if(sortStr == "Year") m_sortMode = SortMode::Year;
        else if(sortStr == "YearDesc") m_sortMode = SortMode::YearDesc;
        else if(sortStr == "Rating") m_sortMode = SortMode::Rating;
        else if(sortStr == "PlayCount") m_sortMode = SortMode::PlayCount;
        else if(sortStr == "Recent") m_sortMode = SortMode::Recent;
        else m_sortMode = SortMode::Random;
    }

    setMouseTracking(true);
    connect(m_animTimer, &QTimer::timeout, this, &AlbumMosaicWidget::triggerAnimation);
    if(m_enableAnim) {
        m_animTimer->start(m_animInterval);
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

    // Fooyin styled ToolTip — top-level so it's not clipped by widget bounds
    m_toolTip = new Fooyin::ToolTip(nullptr);
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
            update();
        });
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

    // Bug 2: Disconnect before reconnect to avoid signal leak
    if(m_coverProvider) {
        disconnect(m_coverProvider, &Fooyin::CoverProvider::coverAdded, this, nullptr);
        connect(m_coverProvider, &Fooyin::CoverProvider::coverAdded, this, [this](const Fooyin::Track& track) {
            // Clear scaled cache for this track's album so it gets re-fetched at next paint
            for(int i = 0; i < m_albums.size(); ++i) {
                const AlbumInfo& album = m_albums[i];
                if(album.track.isValid() && album.track.id() == track.id()) {
                    m_scaledCache.remove(i);
                    // Start fade-in from 0
                    m_coverFadeProgress[i] = 0;
                    if(m_fadeTimer && !m_fadeTimer->isActive()) {
                        m_fadeTimer->start();
                    }
                    break;
                }
            }
            update();
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

void AlbumMosaicWidget::triggerAnimation()
{
    if(m_coverPositions.isEmpty() || m_albumOrder.isEmpty() || !m_coverProvider) {
        return;
    }

    // Duration based on speed setting
    const int durationMs = [this]() {
        switch(m_animSpeed) {
            case AnimSpeed::Fast: return 300;
            case AnimSpeed::Slow: return 1000;
            default: return 600;
        }
    }();

    const auto coverSize = Fooyin::CoverProvider::findThumbnailSize(m_coverPositions[0].size());
    const int cols = m_columnCount;

    // Start new animations if none are active (timer-triggered)
    if(m_activeAnims.isEmpty()) {
        const int numCells = m_coverPositions.size();

        if(m_animScope == AnimScope::Single) {
            // Single cell animation (original behavior)
            ActiveAnim anim;
            anim.cellIndex = QRandomGenerator::global()->bounded(numCells);
            const int row = anim.cellIndex / cols;
            const int col = anim.cellIndex % cols;
            const int globalIndex = (row + m_scrollOffset) * cols + col;
            anim.orderIndex = ((globalIndex % m_albumOrder.size()) + m_albumOrder.size()) % m_albumOrder.size();
            anim.oldAlbumIndex = m_albumOrder[anim.orderIndex];

            int swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
            while(m_albumOrder.size() > 1 && (swapWith == anim.orderIndex
                   || m_albumOrder[swapWith] == m_lastSwappedCellA
                   || m_albumOrder[swapWith] == m_lastSwappedCellB)) {
                swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
            }
            anim.newAlbumIndex = m_albumOrder[swapWith];
            anim.swapWithOrderIndex = swapWith;
            m_lastSwappedCellA = anim.oldAlbumIndex;
            m_lastSwappedCellB = anim.newAlbumIndex;

            // Pre-load the new album cover (cached, non-blocking if already cached)
            if(anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
                getCoverForPaint(anim.newAlbumIndex, m_coverPositions[0].size(), coverSize);
            }
            anim.animType = effectiveAnimType();
            anim.elapsed.start();
            m_activeAnims.append(anim);
        } else if(m_animScope == AnimScope::Multiple) {
            // Multiple random cells animate simultaneously (max 5 to limit CPU/GPU)
            const int numToAnimate = std::min(5, numCells);
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

                int swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
                while(m_albumOrder.size() > 1 && (swapWith == anim.orderIndex || usedOrders.contains(swapWith)
                       || m_albumOrder[swapWith] == m_lastSwappedCellA
                       || m_albumOrder[swapWith] == m_lastSwappedCellB)) {
                    swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
                }
                usedOrders.insert(swapWith);
                anim.newAlbumIndex = m_albumOrder[swapWith];
                anim.swapWithOrderIndex = swapWith;
                m_lastSwappedCellA = anim.oldAlbumIndex;
                m_lastSwappedCellB = anim.newAlbumIndex;

                if(anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
                    getCoverForPaint(anim.newAlbumIndex, m_coverPositions[0].size(), coverSize);
                }
                anim.animType = effectiveAnimType();
            anim.elapsed.start();
                m_activeAnims.append(anim);
            }
        } else if(m_animScope == AnimScope::Wave) {
            // Wave: animate one cell per column, sweeping left to right
            // All columns participate; the delay is spread across a fixed total
            // window so it stays reasonable regardless of column count.
            const int numCols = cols; // Use all columns
            const int waveTotalDelayMs = 800; // Total spread across the wave
            const int perColDelay = numCols > 1 ? waveTotalDelayMs / (numCols - 1) : 0;
            for(int col = 0; col < numCols; ++col) {
                // Pick one random row per column
                const int visibleRows = m_coverPositions.size() / cols;
                if(visibleRows <= 0) continue;
                const int row = QRandomGenerator::global()->bounded(visibleRows);
                const int cell = row * cols + col;
                if(cell >= m_coverPositions.size()) continue;

                ActiveAnim anim;
                anim.cellIndex = cell;
                anim.delayMs = col * perColDelay; // Wave delay: spread across fixed window

                const int globalIndex = (row + m_scrollOffset) * cols + col;
                anim.orderIndex = ((globalIndex % m_albumOrder.size()) + m_albumOrder.size()) % m_albumOrder.size();
                anim.oldAlbumIndex = m_albumOrder[anim.orderIndex];

                int swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
                while(m_albumOrder.size() > 1 && (swapWith == anim.orderIndex
                       || m_albumOrder[swapWith] == m_lastSwappedCellA
                       || m_albumOrder[swapWith] == m_lastSwappedCellB)) {
                    swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
                }
                anim.newAlbumIndex = m_albumOrder[swapWith];
                anim.swapWithOrderIndex = swapWith;
                m_lastSwappedCellA = anim.oldAlbumIndex;
                m_lastSwappedCellB = anim.newAlbumIndex;

                if(anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
                    getCoverForPaint(anim.newAlbumIndex, m_coverPositions[0].size(), coverSize);
                }
                anim.animType = effectiveAnimType();
            anim.elapsed.start();
                m_activeAnims.append(anim);
            }
        }
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

        // At 50%: check if the new cover is ready. If yes, swap m_albumOrder.
        if(progress >= 0.5f && !anim.newCoverReady && anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
            const AlbumInfo& newAlbum = m_albums[anim.newAlbumIndex];
            if(newAlbum.track.isValid()) {
                // Check if cover is already cached (non-blocking)
                if(m_scaledCache.contains(anim.newAlbumIndex)) {
                    anim.newCoverReady = true;
                    if(anim.swapWithOrderIndex >= 0 && anim.swapWithOrderIndex < m_albumOrder.size()) {
                        m_albumOrder[anim.orderIndex] = anim.newAlbumIndex;
                        m_albumOrder[anim.swapWithOrderIndex] = anim.oldAlbumIndex;
                    }
                    if(anim.cellIndex < m_currentGridIndices.size()) {
                        m_currentGridIndices[anim.cellIndex] = anim.newAlbumIndex;
                    }
                }
                // If not cached, request async — newCoverReady stays false until loaded
            }
        }

        // Remove completed animations
        if(progress >= 1.0f) {
            m_activeAnims.removeAt(i);
        }
    }

    update();

    if(!m_activeAnims.isEmpty()) {
        QTimer::singleShot(33, this, &AlbumMosaicWidget::triggerAnimation); // ~30fps
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
        updateMosaic();
        update();
        updateVisibleThumbnailKeys();
        event->accept();
        return;
    }

    const int scrollAmount = delta > 0 ? -1 : 1;

    m_scrollOffset += scrollAmount;

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
    m_rightClickedCellIndex = -1;
    for(int i = 0; i < m_coverPositions.size(); ++i) {
        if(m_coverPositions[i].contains(event->pos())) {
            m_rightClickedCellIndex = i;
            break;
        }
    }

    QMenu menu(this);

    if(m_rightClickedCellIndex != -1 && m_rightClickedCellIndex < m_currentGridIndices.size()) {
        int albumIndex = m_currentGridIndices[m_rightClickedCellIndex];
        if(albumIndex < m_albums.size()) {
            const AlbumInfo& album = m_albums[albumIndex];
            Fooyin::TrackList albumTracks = getAlbumTracks(album.album, album.albumArtist);

            // Explicit "Play Album" action — always available, clearly labeled
            QAction* playAction = menu.addAction(tr("Play Album"));
            connect(playAction, &QAction::triggered, this, [this, album]() {
                playAlbum(album.album, album.albumArtist);
            });

            // "Play and Replace Queue" — clears the queue, then plays the album
            QAction* playReplaceQueueAction = menu.addAction(tr("Play and Replace Queue"));
            connect(playReplaceQueueAction, &QAction::triggered, this, [this, album, albumTracks]() {
                if(m_coreContext && m_coreContext->playerController) {
                    m_coreContext->playerController->clearQueue();
                    m_coreContext->playerController->queueTracks(albumTracks);
                }
                playAlbum(album.album, album.albumArtist);
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
            QMenu* albumMenu = menu.addMenu(tr("Album"));
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
    const QString key = album + "|" + albumArtist;
    if(m_albumTracksCache.contains(key)) {
        Fooyin::TrackList tracks = m_albumTracksCache[key];
        std::sort(tracks.begin(), tracks.end(), [](const Fooyin::Track& a, const Fooyin::Track& b) {
            return a.trackNumber() < b.trackNumber();
        });
        return tracks;
    }
    return {};
}

void AlbumMosaicWidget::queueAlbum(const QString& album, const QString& albumArtist)
{
    Fooyin::TrackList albumTracks = getAlbumTracks(album, albumArtist);

    if(albumTracks.empty()) {
        return;
    }

    if(m_coreContext && m_coreContext->playerController) {
        m_coreContext->playerController->queueTracks(albumTracks);
    }
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
    if(!m_coreContext || !m_coreContext->settingsManager) {
        return;
    }

    AlbumMosaicSettingsDialog dialog(m_coreContext->settingsManager, m_coreContext->library, this);
    dialog.exec();

    loadSettings();
}

void AlbumMosaicWidget::addQuickSettings(QMenu* menu)
{
    if(!m_coreContext || !m_coreContext->settingsManager) {
        return;
    }

    auto* settings = m_coreContext->settingsManager;

    // Animation toggle
    QAction* animToggle = menu->addAction(tr("Animation"));
    animToggle->setCheckable(true);
    animToggle->setChecked(m_enableAnim);
    connect(animToggle, &QAction::triggered, this, [this, settings](bool checked) {
        m_enableAnim = checked;
        settings->set(QStringLiteral("AlbumMosaic/EnableAnim"), checked);
        if(checked) {
            m_animTimer->start(m_animInterval);
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
            settings->set(QStringLiteral("AlbumMosaic/AnimType"),
                          type == AnimType::Flip3D ? "Flip3D" :
                          type == AnimType::Crossfade ? "Crossfade" :
                          type == AnimType::Slide ? "Slide" :
                          type == AnimType::Zoom ? "Zoom" :
                          type == AnimType::PageCurl ? "PageCurl" : "Random");
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
            settings->set(QStringLiteral("AlbumMosaic/AnimSpeed"),
                          speed == AnimSpeed::Fast ? "Fast" :
                          speed == AnimSpeed::Slow ? "Slow" : "Medium");
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
            settings->set(QStringLiteral("AlbumMosaic/AnimScope"),
                          scope == AnimScope::Multiple ? "Multiple" :
                          scope == AnimScope::Wave ? "Wave" : "Single");
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
            settings->set(QStringLiteral("AlbumMosaic/SortMode"),
                          mode == SortMode::Year ? "Year" :
                          mode == SortMode::YearDesc ? "YearDesc" :
                          mode == SortMode::Rating ? "Rating" :
                          mode == SortMode::PlayCount ? "PlayCount" :
                          mode == SortMode::Recent ? "Recent" : "Random");
            randomizeGrid();
            invalidateScaledCache();
            update();
        });
    };
    addSortAction(tr("Random"), SortMode::Random);
    addSortAction(tr("Year (newest first)"), SortMode::YearDesc);
    addSortAction(tr("Year (oldest first)"), SortMode::Year);
    addSortAction(tr("Rating (highest first)"), SortMode::Rating);
    addSortAction(tr("Play Count"), SortMode::PlayCount);
    addSortAction(tr("Recently Played"), SortMode::Recent);
}

void AlbumMosaicWidget::loadSettings()
{
    if(m_coreContext && m_coreContext->settingsManager) {
        m_enableAnim = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/EnableAnim")).toBool();
        m_animInterval = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimInterval")).toInt();
        m_columnCount = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt();
        m_genreFilter = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/GenreFilter")).toString();
        m_artistFilter = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/ArtistFilter")).toString();

        // Load animation type
        const QString animTypeStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimType")).toString();
        if(animTypeStr == "Crossfade") m_animType = AnimType::Crossfade;
        else if(animTypeStr == "Slide") m_animType = AnimType::Slide;
        else if(animTypeStr == "Zoom") m_animType = AnimType::Zoom;
        else if(animTypeStr == "PageCurl") m_animType = AnimType::PageCurl;
        else if(animTypeStr == "Random") m_animType = AnimType::Random;
        else m_animType = AnimType::Flip3D;

        // Load animation speed
        const QString speedStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimSpeed")).toString();
        if(speedStr == "Fast") m_animSpeed = AnimSpeed::Fast;
        else if(speedStr == "Slow") m_animSpeed = AnimSpeed::Slow;
        else m_animSpeed = AnimSpeed::Medium;

        // Load animation scope
        const QString scopeStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/AnimScope")).toString();
        if(scopeStr == "Multiple") m_animScope = AnimScope::Multiple;
        else if(scopeStr == "Wave") m_animScope = AnimScope::Wave;
        else m_animScope = AnimScope::Single;

        // Load background color
        m_bgColor = QColor(m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/BgColor")).toString());
        if(!m_bgColor.isValid()) m_bgColor = Qt::black;

        // Load sort mode
        const QString sortStr = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/SortMode")).toString();
        if(sortStr == "Year") m_sortMode = SortMode::Year;
        else if(sortStr == "YearDesc") m_sortMode = SortMode::YearDesc;
        else if(sortStr == "Rating") m_sortMode = SortMode::Rating;
        else if(sortStr == "PlayCount") m_sortMode = SortMode::PlayCount;
        else if(sortStr == "Recent") m_sortMode = SortMode::Recent;
        else m_sortMode = SortMode::Random;

        // Sync inline sort combo
        if(m_sortCombo) {
            int idx = m_sortCombo->findData(sortStr);
            if(idx >= 0 && idx != m_sortCombo->currentIndex()) {
                QSignalBlocker blocker(m_sortCombo);
                m_sortCombo->setCurrentIndex(idx);
            }
        }

        if(m_enableAnim) {
            m_animTimer->start(m_animInterval);
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
    if(sortStr == "Year") m_sortMode = SortMode::Year;
    else if(sortStr == "YearDesc") m_sortMode = SortMode::YearDesc;
    else if(sortStr == "Rating") m_sortMode = SortMode::Rating;
    else if(sortStr == "PlayCount") m_sortMode = SortMode::PlayCount;
    else if(sortStr == "Recent") m_sortMode = SortMode::Recent;
    else m_sortMode = SortMode::Random;

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

int AlbumMosaicWidget::findAlbumCell(const QString& album, const QString& albumArtist) const
{
    for(int i = 0; i < m_currentGridIndices.size() && i < m_coverPositions.size(); ++i) {
        int albumIndex = m_currentGridIndices[i];
        if(albumIndex < m_albums.size()) {
            const AlbumInfo& info = m_albums[albumIndex];
            if(info.album == album && info.albumArtist == albumArtist) {
                return i;
            }
        }
    }
    return -1;
}

void AlbumMosaicWidget::searchEvent(const Fooyin::SearchRequest& request)
{
    // Feature: Search integration
    m_searchQuery = request.text;
    loadAlbumMetadata();
    update();
}

void AlbumMosaicWidget::saveLayoutData(QJsonObject& layout)
{
    // Bug 7: Persist scroll offset and filters in layout
    layout[QStringLiteral("scrollOffset")] = m_scrollOffset;
}

void AlbumMosaicWidget::loadLayoutData(const QJsonObject& layout)
{
    // Bug 7: Restore scroll offset from layout
    if(layout.contains(QStringLiteral("scrollOffset"))) {
        m_scrollOffset = layout.value(QStringLiteral("scrollOffset")).toInt();
    }
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

    // If cell size changed, clear scaled cache (covers will be re-scaled on next paint)
    if(m_scaledCacheCellSize != m_coverPositions[0].size()) {
        m_scaledCache.clear();
        m_scaledCacheCellSize = m_coverPositions[0].size();
    }

    // Duration based on speed setting (must match triggerAnimation)
    const int durationMs = [this]() {
        switch(m_animSpeed) {
            case AnimSpeed::Fast: return 300;
            case AnimSpeed::Slow: return 1000;
            default: return 600;
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

                // Feature: Highlight currently playing album
                const bool isPlaying = (album.album == m_currentPlayingAlbum
                                        && album.albumArtist == m_currentPlayingArtist);

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

                    QRect destRect = scaledCover.rect();
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
                                animRect = animCover.rect();
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
                    // Placeholder already drawn above, just add hover/playing/text overlay

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
    invalidateScaledCache();
    updateMosaic();
    update();
    updateVisibleThumbnailKeys();
}

void AlbumMosaicWidget::invalidateScaledCache()
{
    m_scaledCache.clear();
    m_scaledCacheCellSize = QSize(0, 0);
}

QPixmap AlbumMosaicWidget::getCoverForPaint(int albumIndex, const QSize& cellSize, const Fooyin::ThumbnailSize& coverSize)
{
    // Return scaled cache if available and cell size matches
    if(m_scaledCache.contains(albumIndex) && m_scaledCacheCellSize == cellSize) {
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

    QPixmap cover = m_coverProvider->trackCoverThumbnail(album.track, coverSize);

    // Check if CoverProvider returned the placeholder (cover not yet loaded)
    if(cover.isNull() || cover.cacheKey() == m_placeholderCacheKey) {
        return {}; // coverAdded will fire when the real cover is available
    }

    // Scale to cell size and cache
    while(m_scaledCache.size() >= MAX_CACHE_SIZE) {
        m_scaledCache.remove(m_scaledCache.begin().key());
    }

    QPixmap scaled = cover.scaled(cellSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
        keys.insert(m_coverProvider->thumbnailCacheKey(album.track, coverSize));
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
