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
#include <QDateTime>
#include <algorithm>
#include <QLinearGradient>
#include <QFont>
#include <QWheelEvent>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QPainterPath>
#include <QMenu>
#include <QContextMenuEvent>
#include <QMessageBox>
#include <cmath>
#include <core/library/musiclibrary.h>
#include <core/player/playercontroller.h>
#include <core/track.h>
#include <core/engine/audioloader.h>
#include <gui/coverprovider.h>
#include <gui/trackselectioncontroller.h>
#include <core/plugins/coreplugincontext.h>
#include <utils/settings/settingsmanager.h>

AlbumMosaicWidget::AlbumMosaicWidget(Fooyin::GuiPluginContext* guiContext, Fooyin::CorePluginContext* coreContext, Fooyin::CoverProvider* coverProvider, QWidget* parent)
    : FyWidget{parent}
    , m_guiContext{guiContext}
    , m_coreContext{coreContext}
    , m_coverProvider{coverProvider}
    , m_flipTimer{new QTimer(this)}
    , m_currentFlipIndex{0}
    , m_isFlipping{false}
    , m_flipProgress{0.0f}
{
    // Load settings from SettingsManager
    if(m_coreContext && m_coreContext->settingsManager) {
        m_enableFlip = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/EnableFlip")).toBool();
        m_flipInterval = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/FlipInterval")).toInt();
        m_columnCount = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt();
    }
    
    setMouseTracking(true); // Enable mouse tracking for tooltips
    connect(m_flipTimer, &QTimer::timeout, this, &AlbumMosaicWidget::flipAnimation);
    if(m_enableFlip) {
        m_flipTimer->start(m_flipInterval);
    }
    
    loadAlbumMetadata();
    updateMosaic();
    randomizeGrid();
}

AlbumMosaicWidget::~AlbumMosaicWidget()
{
    delete m_flipTimer;
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
    // Load album metadata from MusicLibrary
    if(!m_coreContext || !m_coreContext->library) {
        qDebug() << "[METADATA] MusicLibrary not available";
        return;
    }
    
    Fooyin::TrackList tracks = m_coreContext->library->tracks();
    qDebug() << "[METADATA] Total tracks in library:" << tracks.size();
    
    // If library is empty, schedule a retry
    if(tracks.empty()) {
        qDebug() << "[METADATA] Library is empty, will retry in 2 seconds";
        QTimer::singleShot(2000, this, &AlbumMosaicWidget::loadAlbumMetadata);
        return;
    }
    
    QSet<QString> uniqueAlbums;
    
    for(const Fooyin::Track& track : tracks) {
        QString album = track.album();
        QString albumArtist = track.albumArtist();
        
        if(album.isEmpty() || albumArtist.isEmpty()) {
            continue;
        }
        
        QString albumKey = album + "|" + albumArtist;
        if(!uniqueAlbums.contains(albumKey)) {
            uniqueAlbums.insert(albumKey);
            
            // Store album metadata with track reference for CoverProvider
            AlbumInfo info;
            info.album = album;
            info.albumArtist = albumArtist;
            info.filePath = track.filepath();
            info.track = track; // Store the track for CoverProvider
            info.coverPath.clear(); // CoverProvider will handle this
            m_albums.append(info);
        }
    }
    
    // Shuffle albums for more random display
    std::shuffle(m_albums.begin(), m_albums.end(), *QRandomGenerator::global());
    
    qDebug() << "Loaded" << m_albums.size() << "albums from Fooyin library (metadata only, shuffled)";
    
    // Update mosaic and grid after loading metadata
    updateMosaic();
    randomizeGrid();
    
    // Connect to CoverProvider's coverAdded signal to repaint when covers are loaded
    if(m_coverProvider) {
        connect(m_coverProvider, &Fooyin::CoverProvider::coverAdded, this, [this](const Fooyin::Track& track) {
            // Check if this track is in our album list
            for(const AlbumInfo& album : m_albums) {
                if(album.track.isValid() && album.track.id() == track.id()) {
                    update(); // Repaint when cover for this album is loaded
                    break;
                }
            }
        });
    }
}

void AlbumMosaicWidget::updateMosaic()
{
    m_coverPositions.clear();
    
    if(m_albums.isEmpty()) {
        return;
    }
    
    const int cols = m_columnCount;
    const int cellWidth = width() / cols;
    const int cellHeight = cellWidth; // Square cells
    const int visibleRows = (height() + cellHeight - 1) / cellHeight;
    const int totalCells = cols * visibleRows;
    
    // Calculate cell positions with scroll offset (infinite scrolling)
    for(int row = 0; row < visibleRows; ++row) {
        for(int col = 0; col < cols; ++col) {
            const int globalRow = row + m_scrollOffset;
            const int globalIndex = globalRow * cols + col;
            
            // Use modulo to cycle through albums infinitely
            // Handle negative indices by adding size before modulo
            const int albumIndex = ((globalIndex % m_albums.size()) + m_albums.size()) % m_albums.size();
            
            const int x = col * cellWidth;
            const int y = row * cellHeight;
            
            m_coverPositions.append(QRect(x, y, cellWidth, cellHeight));
            
            // Update current grid indices to match positions
            if(m_currentGridIndices.size() <= m_coverPositions.size()) {
                m_currentGridIndices.append(albumIndex);
            } else {
                m_currentGridIndices[m_coverPositions.size() - 1] = albumIndex;
            }
        }
    }
    
    qDebug() << "Updated infinite mosaic with" << m_coverPositions.size() << "cells, scroll offset:" << m_scrollOffset;
}

void AlbumMosaicWidget::randomizeGrid()
{
    if(m_albums.isEmpty() || m_coverPositions.isEmpty()) {
        return;
    }
    
    m_currentGridIndices.clear();
    
    // Fill grid with random album indices
    for(int i = 0; i < m_coverPositions.size(); ++i) {
        int randomIndex = QRandomGenerator::global()->bounded(m_albums.size());
        m_currentGridIndices.append(randomIndex);
    }
    
    qDebug() << "Randomized grid with" << m_currentGridIndices.size() << "cells from" << m_albums.size() << "albums";
}

void AlbumMosaicWidget::flipAnimation()
{
    if(m_coverPositions.isEmpty()) {
        return;
    }
    
    // Select random cover position to flip
    if(!m_isFlipping) {
        m_currentFlipIndex = QRandomGenerator::global()->bounded(m_coverPositions.size());
        m_isFlipping = true;
        m_flipProgress = 0.0f;
        m_flipDirection = 1;
        
        // Store old album index
        if(m_currentFlipIndex < m_currentGridIndices.size()) {
            m_oldAlbumIndex = m_currentGridIndices[m_currentFlipIndex];
        }
        
        // Select new random album (different from old one)
        int newAlbumIndex = QRandomGenerator::global()->bounded(m_albums.size());
        while(m_albums.size() > 1 && newAlbumIndex == m_oldAlbumIndex) {
            newAlbumIndex = QRandomGenerator::global()->bounded(m_albums.size());
        }
        m_newAlbumIndex = newAlbumIndex;
        
        // Update the grid index to new album
        if(m_currentFlipIndex < m_currentGridIndices.size()) {
            m_currentGridIndices[m_currentFlipIndex] = newAlbumIndex;
        }
    }
    
    // Animate flip progress (smooth sine wave for realistic motion)
    m_flipProgress += 0.05f;
    
    // Flip direction changes at 50% progress
    if(m_flipProgress >= 0.5f && m_flipDirection == 1) {
        m_flipDirection = -1;
    }
    
    // Animation complete when progress reaches 1.0
    if(m_flipProgress >= 1.0f) {
        m_isFlipping = false;
        m_flipProgress = 0.0f;
    }
    
    update();
    
    // Continue animation
    if(m_isFlipping) {
        QTimer::singleShot(16, this, &AlbumMosaicWidget::flipAnimation); // ~60fps
    }
}

void AlbumMosaicWidget::wheelEvent(QWheelEvent* event)
{
    const int delta = event->angleDelta().y();
    const int scrollAmount = delta > 0 ? -1 : 1; // Scroll up = decrease offset, scroll down = increase offset
    
    // No scroll limit for infinite grid
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
    
    // Find which cell is being hovered
    int oldHoveredIndex = m_hoveredCellIndex;
    m_hoveredCellIndex = -1;
    
    for(int i = 0; i < m_coverPositions.size(); ++i) {
        const QRect& cell = m_coverPositions[i];
        if(cell.contains(event->pos())) {
            m_hoveredCellIndex = i;
            
            // Get the album at this grid position
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
    
    // Repaint if hover state changed
    if(oldHoveredIndex != m_hoveredCellIndex) {
        update();
    }
    
    // Clear tooltip if not hovering over any cell
    if(m_hoveredCellIndex == -1) {
        setToolTip("");
    }
}

void AlbumMosaicWidget::mousePressEvent(QMouseEvent* event)
{
    // Single-click does nothing, only double-click plays
    Q_UNUSED(event)
}

void AlbumMosaicWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if(event->button() == Qt::LeftButton) {
        // Check which cell was double-clicked
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
    // Find which cell was right-clicked
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
    
    // If no album was clicked, show general settings
    QAction* settingsAction = menu.addAction(tr("Settings"));
    QAction* selectedAction = menu.exec(event->globalPos());
    
    if(selectedAction == settingsAction) {
        showSettingsDialog();
    }
}

void AlbumMosaicWidget::queueAlbum(const QString& album, const QString& albumArtist)
{
    if(!m_coreContext || !m_coreContext->library) {
        qDebug() << "MusicLibrary not available for queuing";
        return;
    }
    
    // Get all tracks for this album from MusicLibrary
    Fooyin::TrackList tracks = m_coreContext->library->tracks();
    Fooyin::TrackList albumTracks;
    
    for(const Fooyin::Track& track : tracks) {
        if(track.album() == album && track.albumArtist() == albumArtist) {
            if(track.isValid()) {
                albumTracks.push_back(track);
            }
        }
    }
    
    if(albumTracks.empty()) {
        qDebug() << "No tracks found for album:" << album << "by" << albumArtist;
        return;
    }
    
    // Sort by track number
    std::sort(albumTracks.begin(), albumTracks.end(), [](const Fooyin::Track& a, const Fooyin::Track& b) {
        return a.trackNumber() < b.trackNumber();
    });
    
    qDebug() << "Queuing album:" << album << "by" << albumArtist << "with" << albumTracks.size() << "tracks";
    
    // Use PlayerController to queue the album
    if(m_coreContext && m_coreContext->playerController) {
        m_coreContext->playerController->queueTracks(albumTracks);
    }
}

void AlbumMosaicWidget::showAlbumInfo(const AlbumInfo& album)
{
    QString info = tr("Album: %1\nArtist: %2\nPath: %3").arg(album.album, album.albumArtist, album.filePath);
    QMessageBox::information(this, tr("Album Information"), info);
}

void AlbumMosaicWidget::showSettingsDialog()
{
    if(!m_coreContext || !m_coreContext->settingsManager) {
        return;
    }
    
    AlbumMosaicSettingsDialog dialog(m_coreContext->settingsManager, this);
    dialog.exec();
    
    // Reload settings after dialog closes
    loadSettings();
}

void AlbumMosaicWidget::loadSettings()
{
    if(m_coreContext && m_coreContext->settingsManager) {
        m_enableFlip = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/EnableFlip")).toBool();
        m_flipInterval = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/FlipInterval")).toInt();
        m_columnCount = m_coreContext->settingsManager->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt();
        
        // Restart flip timer with new settings
        if(m_enableFlip) {
            m_flipTimer->start(m_flipInterval);
        } else {
            m_flipTimer->stop();
        }
        
        // Update mosaic with new column count
        updateMosaic();
        update();
    }
}

void AlbumMosaicWidget::playAlbum(const QString& album, const QString& albumArtist)
{
    if(!m_coreContext || !m_coreContext->library) {
        qDebug() << "MusicLibrary not available for playback";
        return;
    }
    
    // Get all tracks for this album from MusicLibrary
    Fooyin::TrackList tracks = m_coreContext->library->tracks();
    Fooyin::TrackList albumTracks;
    
    for(const Fooyin::Track& track : tracks) {
        if(track.album() == album && track.albumArtist() == albumArtist) {
            if(track.isValid()) {
                albumTracks.push_back(track);
            }
        }
    }
    
    if(albumTracks.empty()) {
        qDebug() << "No tracks found for album:" << album << "by" << albumArtist;
        return;
    }
    
    // Sort by track number
    std::sort(albumTracks.begin(), albumTracks.end(), [](const Fooyin::Track& a, const Fooyin::Track& b) {
        return a.trackNumber() < b.trackNumber();
    });
    
    qDebug() << "Playing album:" << album << "by" << albumArtist << "with" << albumTracks.size() << "tracks";
    
    // Use PlayerController to play the album
    if(m_coreContext && m_coreContext->playerController) {
        // Clear queue and replace tracks
        m_coreContext->playerController->clearQueue();
        m_coreContext->playerController->replaceTracks(albumTracks);
        m_coreContext->playerController->play();
    }
}

void AlbumMosaicWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    
    QPainter painter(this);
    painter.fillRect(rect(), Qt::black);
    
    if(m_albums.isEmpty() || m_coverPositions.isEmpty()) {
        return;
    }
    
    for(int i = 0; i < m_coverPositions.size(); ++i) {
        const QRect& cell = m_coverPositions[i];
        const bool isHovered = (i == m_hoveredCellIndex);
        const bool isFlipping = (m_isFlipping && i == m_currentFlipIndex);
        
        // Get the album at this grid position
        if(i < m_currentGridIndices.size()) {
            int albumIndex = m_currentGridIndices[i];
            if(albumIndex < m_albums.size()) {
                const AlbumInfo& album = m_albums[albumIndex];
                
                // Try to get cover from CoverProvider (uses its static cache)
                QPixmap cover;
                if(m_coverProvider && album.track.isValid()) {
                    cover = m_coverProvider->trackCoverThumbnail(album.track, Fooyin::CoverProvider::VeryLarge);
                }
                
                if(!cover.isNull()) {
                    // Scale cover to fit cell while maintaining aspect ratio
                    QPixmap scaledCover = cover.scaled(cell.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    
                    // Draw scaled cover centered in cell
                    QRect destRect = scaledCover.rect();
                    destRect.moveCenter(cell.center());
                    
                    // Apply 3D flip transformation if flipping
                    if(isFlipping) {
                        painter.save();
                        
                        // Calculate flip angle (0 to 180 degrees)
                        float angle = m_flipProgress * 180.0f;
                        
                        // Calculate scale based on angle (cosine for 3D perspective)
                        float scale = std::cos(angle * M_PI / 180.0f);
                        if(scale < 0) scale = -scale; // Always positive scale
                        
                        // Determine which album to show based on flip direction
                        int albumToShow = (m_flipProgress < 0.5f) ? m_oldAlbumIndex : m_newAlbumIndex;
                        if(albumToShow >= 0 && albumToShow < m_albums.size()) {
                            const AlbumInfo& flipAlbum = m_albums[albumToShow];
                            QPixmap flipCover;
                            if(m_coverProvider && flipAlbum.track.isValid()) {
                                flipCover = m_coverProvider->trackCoverThumbnail(flipAlbum.track, Fooyin::CoverProvider::VeryLarge);
                            }
                            if(!flipCover.isNull()) {
                                scaledCover = flipCover.scaled(cell.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                                destRect = scaledCover.rect();
                                destRect.moveCenter(cell.center());
                            }
                        }
                        
                        // Apply transformation
                        painter.translate(destRect.center());
                        painter.scale(scale, 1.0f);
                        painter.translate(-destRect.center());
                        
                        // Draw hover effect
                        if(isHovered) {
                            QPainterPath shadowPath;
                            shadowPath.addRect(destRect);
                            painter.setPen(QPen(QColor(255, 255, 255, 100), 3));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawPath(shadowPath);
                            
                            // Slight brightness increase
                            painter.setOpacity(1.1);
                        }
                        
                        painter.drawPixmap(destRect, scaledCover);
                        painter.setOpacity(1.0);
                        
                        painter.restore();
                    } else {
                        // Draw hover effect
                        if(isHovered) {
                            QPainterPath shadowPath;
                            shadowPath.addRect(destRect);
                            painter.setPen(QPen(QColor(255, 255, 255, 100), 3));
                            painter.setBrush(Qt::NoBrush);
                            painter.drawPath(shadowPath);
                            
                            // Slight brightness increase
                            painter.setOpacity(1.1);
                            painter.drawPixmap(destRect, scaledCover);
                            painter.setOpacity(1.0);
                        } else {
                            painter.drawPixmap(destRect, scaledCover);
                        }
                    }
                    
                    // Draw album name overlay on cover (semi-transparent)
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
                        
                        // Draw semi-transparent background
                        painter.fillRect(textRect, QColor(0, 0, 0, 150));
                        painter.drawText(textRect, Qt::AlignCenter, shortName);
                    }
                } else {
                    // Draw attractive placeholder with gradient
                    QLinearGradient gradient(cell.topLeft(), cell.bottomRight());
                    gradient.setColorAt(0, QColor(60, 60, 70));
                    gradient.setColorAt(1, QColor(40, 40, 50));
                    painter.fillRect(cell, gradient);
                    
                    // Draw hover effect on placeholder
                    if(isHovered) {
                        painter.setPen(QPen(QColor(255, 255, 255, 100), 3));
                        painter.setBrush(Qt::NoBrush);
                        painter.drawRect(cell);
                    }
                    
                    // Draw musical note icon
                    painter.setPen(QColor(200, 200, 200));
                    QFont font = painter.font();
                    font.setPixelSize(cell.height() / 3);
                    painter.setFont(font);
                    painter.drawText(cell, Qt::AlignCenter, "♪");
                    
                    // Draw album name below note
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
