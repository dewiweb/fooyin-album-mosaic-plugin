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
#include <QDebug>
#include <algorithm>
#include <numeric>
#include <QLinearGradient>
#include <QFont>
#include <QPainterPath>
#include <QMenu>
#include <QMessageBox>
#include <QJsonObject>
#include <cmath>
#include <core/library/musiclibrary.h>
#include <core/player/playercontroller.h>
#include <core/playlist/playlisthandler.h>
#include <core/playlist/playlist.h>
#include <core/track.h>
#include <gui/coverprovider.h>
#include <gui/coverartworktypes.h>
#include <gui/trackselectioncontroller.h>
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
    }

    setMouseTracking(true);
    connect(m_animTimer, &QTimer::timeout, this, &AlbumMosaicWidget::triggerAnimation);
    if(m_enableAnim) {
        m_animTimer->start(m_animInterval);
    }

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
    loadAlbumMetadata();
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
            for(const AlbumInfo& album : m_albums) {
                if(album.track.isValid() && album.track.id() == track.id()) {
                    update();
                    break;
                }
            }
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

    // Build a shuffled permutation of album indices.
    // This stays fixed until albums change — scrolling uses updateMosaic()
    // to slide a window through this permutation.
    m_albumOrder.resize(m_albums.size());
    std::iota(m_albumOrder.begin(), m_albumOrder.end(), 0);
    std::shuffle(m_albumOrder.begin(), m_albumOrder.end(), *QRandomGenerator::global());

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
            while(m_albumOrder.size() > 1 && swapWith == anim.orderIndex) {
                swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
            }
            anim.newAlbumIndex = m_albumOrder[swapWith];
            anim.swapWithOrderIndex = swapWith;

            // Trigger async cover load
            if(anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
                const AlbumInfo& newAlbum = m_albums[anim.newAlbumIndex];
                if(newAlbum.track.isValid()) {
                    m_coverProvider->trackCoverThumbnail(newAlbum.track, coverSize);
                }
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
                while(m_albumOrder.size() > 1 && (swapWith == anim.orderIndex || usedOrders.contains(swapWith))) {
                    swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
                }
                usedOrders.insert(swapWith);
                anim.newAlbumIndex = m_albumOrder[swapWith];
                anim.swapWithOrderIndex = swapWith;

                if(anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
                    const AlbumInfo& newAlbum = m_albums[anim.newAlbumIndex];
                    if(newAlbum.track.isValid()) {
                        m_coverProvider->trackCoverThumbnail(newAlbum.track, coverSize);
                    }
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
                while(m_albumOrder.size() > 1 && swapWith == anim.orderIndex) {
                    swapWith = QRandomGenerator::global()->bounded(m_albumOrder.size());
                }
                anim.newAlbumIndex = m_albumOrder[swapWith];
                anim.swapWithOrderIndex = swapWith;

                if(anim.newAlbumIndex >= 0 && anim.newAlbumIndex < m_albums.size()) {
                    const AlbumInfo& newAlbum = m_albums[anim.newAlbumIndex];
                    if(newAlbum.track.isValid()) {
                        m_coverProvider->trackCoverThumbnail(newAlbum.track, coverSize);
                    }
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
                QPixmap testCover = m_coverProvider->trackCoverThumbnail(newAlbum.track, coverSize);
                if(!testCover.isNull()) {
                    anim.newCoverReady = true;
                    if(anim.swapWithOrderIndex >= 0 && anim.swapWithOrderIndex < m_albumOrder.size()) {
                        m_albumOrder[anim.orderIndex] = anim.newAlbumIndex;
                        m_albumOrder[anim.swapWithOrderIndex] = anim.oldAlbumIndex;
                    }
                    if(anim.cellIndex < m_currentGridIndices.size()) {
                        m_currentGridIndices[anim.cellIndex] = anim.newAlbumIndex;
                    }
                }
            }
        }

        // Remove completed animations
        if(progress >= 1.0f) {
            m_activeAnims.removeAt(i);
        }
    }

    update();

    if(!m_activeAnims.isEmpty()) {
        QTimer::singleShot(16, this, &AlbumMosaicWidget::triggerAnimation); // ~60fps
    }
}

void AlbumMosaicWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    const int scrollAmount = delta > 0 ? -1 : 1;

    m_scrollOffset += scrollAmount;

    updateMosaic();
    update();

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
                    QString tooltipText = QString("%1\n%2").arg(album.album, album.albumArtist);
                    setToolTip(tooltipText);
                }
            }
            break;
        }
    }

    if(oldHoveredIndex != m_hoveredCellIndex) {
        update();
    }

    if(m_hoveredCellIndex == -1) {
        setToolTip("");
    }
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

            QAction* playAction = menu.addAction(tr("Play Album"));
            QAction* queueAction = menu.addAction(tr("Queue Album"));
            menu.addSeparator();

            // Feature: Add to playlist via TrackSelectionController
            if(m_guiContext && m_guiContext->trackSelection) {
                Fooyin::TrackList albumTracks = getAlbumTracks(album.album, album.albumArtist);
                if(!albumTracks.empty()) {
                    QMenu* addToPlaylistMenu = menu.addMenu(tr("Add to Playlist"));
                    m_guiContext->trackSelection->addTrackAddToPlaylistContextMenu(addToPlaylistMenu);
                }
            }

            QAction* infoAction = menu.addAction(tr("Album Info"));
            menu.addSeparator();
            QAction* settingsAction = menu.addAction(tr("Settings"));

            QAction* selectedAction = menu.exec(event->globalPos());

            if(selectedAction == playAction) {
                playAlbum(album.album, album.albumArtist);
            }
            else if(selectedAction == queueAction) {
                queueAlbum(album.album, album.albumArtist);
            }
            else if(selectedAction == infoAction) {
                showAlbumInfo(album);
            }
            else if(selectedAction == settingsAction) {
                showSettingsDialog();
            }
            return;
        }
    }

    QAction* settingsAction = menu.addAction(tr("Settings"));
    QAction* selectedAction = menu.exec(event->globalPos());

    if(selectedAction == settingsAction) {
        showSettingsDialog();
    }
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

void AlbumMosaicWidget::showAlbumInfo(const AlbumInfo& album)
{
    Fooyin::TrackList tracks = getAlbumTracks(album.album, album.albumArtist);
    uint64_t totalDuration = 0;
    for(const auto& track : tracks) {
        totalDuration += track.duration();
    }
    int totalSecs = static_cast<int>(totalDuration / 1000);
    QString duration = QString("%1:%2").arg(totalSecs / 60).arg(totalSecs % 60, 2, 10, QChar('0'));

    QString info = tr("Album: %1\nArtist: %2\nTracks: %3\nDuration: %4\nPath: %5")
                      .arg(album.album, album.albumArtist)
                      .arg(tracks.size())
                      .arg(duration)
                      .arg(album.filePath);
    QMessageBox::information(this, tr("Album Information"), info);
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

                QPixmap cover;
                if(m_coverProvider && album.track.isValid()) {
                    cover = m_coverProvider->trackCoverThumbnail(album.track, coverSize);
                }

                if(!cover.isNull()) {
                    QPixmap scaledCover = cover.scaled(cell.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
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
                            const AlbumInfo& flipAlbum = m_albums[albumToShow];
                            QPixmap flipCover;
                            if(m_coverProvider && flipAlbum.track.isValid()) {
                                flipCover = m_coverProvider->trackCoverThumbnail(flipAlbum.track, coverSize);
                            }
                            if(!flipCover.isNull()) {
                                animCover = flipCover.scaled(cell.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                                animRect = animCover.rect();
                                animRect.moveCenter(cell.center());
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
                        if(isHovered) {
                            painter.setPen(QPen(QColor(255, 255, 255, 100), 3));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawRect(destRect);
                            painter.drawPixmap(destRect, scaledCover);
                        } else {
                            painter.drawPixmap(destRect, scaledCover);
                        }
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
                    // Draw attractive placeholder with gradient
                    QLinearGradient gradient(cell.topLeft(), cell.bottomRight());
                    gradient.setColorAt(0, QColor(60, 60, 70));
                    gradient.setColorAt(1, QColor(40, 40, 50));
                    painter.fillRect(cell, gradient);

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
}

void AlbumMosaicWidget::resizeEvent(QResizeEvent* event)
{
    Q_UNUSED(event)
    updateMosaic();
    update();
}
