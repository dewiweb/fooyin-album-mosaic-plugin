/*
 * Album Mosaic Plugin
 * Copyright 2024, Your Name
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
#include <core/library/musiclibrary.h>
#include <core/player/playercontroller.h>
#include <core/track.h>
#include <core/engine/audioloader.h>
#include <gui/coverprovider.h>
#include <gui/trackselectioncontroller.h>
#include <core/plugins/coreplugincontext.h>

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
    setMouseTracking(true); // Enable mouse tracking for tooltips
    connect(m_flipTimer, &QTimer::timeout, this, &AlbumMosaicWidget::flipAnimation);
    m_flipTimer->start(3000); // Flip every 3 seconds
    
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
    
    const int cols = 10;
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
            const int albumIndex = globalIndex % m_albums.size();
            
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
    m_currentFlipIndex = QRandomGenerator::global()->bounded(m_coverPositions.size());
    m_isFlipping = true;
    m_flipProgress = 0.0f;
    
    // Change the album at this position to a random one
    if(m_currentFlipIndex < m_currentGridIndices.size()) {
        int newAlbumIndex = QRandomGenerator::global()->bounded(m_albums.size());
        m_currentGridIndices[m_currentFlipIndex] = newAlbumIndex;
    }
    
    // Animate flip (simplified - would need proper animation framework for smooth flip)
    QTimer::singleShot(100, this, [this]() {
        m_flipProgress = 0.5f;
        update();
    });
    
    QTimer::singleShot(200, this, [this]() {
        m_isFlipping = false;
        update();
    });
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
    for(int i = 0; i < m_coverPositions.size(); ++i) {
        const QRect& cell = m_coverPositions[i];
        if(cell.contains(event->pos())) {
            // Get the album at this grid position
            if(i < m_currentGridIndices.size()) {
                int albumIndex = m_currentGridIndices[i];
                if(albumIndex < m_albums.size()) {
                    const AlbumInfo& album = m_albums[albumIndex];
                    QString tooltipText = QString("%1\n%2").arg(album.album, album.albumArtist);
                    setToolTip(tooltipText);
                }
            }
            return;
        }
    }
    
    // Clear tooltip if not hovering over any cell
    setToolTip("");
}

void AlbumMosaicWidget::mousePressEvent(QMouseEvent* event)
{
    if(event->button() == Qt::LeftButton) {
        // Check which cell was clicked
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
        // Replace the current tracks with the album tracks
        m_coreContext->playerController->replaceTracks(albumTracks);
        
        // Change to the first track
        if(!albumTracks.empty()) {
            m_coreContext->playerController->changeCurrentTrack(albumTracks[0]);
        }
        
        // Play
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
                    painter.drawPixmap(destRect, scaledCover);
                } else {
                    // Draw attractive placeholder with gradient
                    QLinearGradient gradient(cell.topLeft(), cell.bottomRight());
                    gradient.setColorAt(0, QColor(60, 60, 70));
                    gradient.setColorAt(1, QColor(40, 40, 50));
                    painter.fillRect(cell, gradient);
                    
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
