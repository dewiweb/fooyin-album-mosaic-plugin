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
#include <QTimer>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QDebug>
#include <QDateTime>
#include <algorithm>
#include <QLinearGradient>
#include <QFont>
#include <QTextStream>
#include <QProcess>
#include <QWheelEvent>
#include <QFuture>
#include <QtConcurrent>
#include <QBuffer>
#include <taglib/tag.h>
#include <taglib/fileref.h>
#include <taglib/tbytevector.h>
#include <taglib/attachedpictureframe.h>
#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/flacfile.h>
#include <taglib/mp4file.h>
#include <core/track.h>
#include <gui/trackselectioncontroller.h>

AlbumMosaicWidget::AlbumMosaicWidget(Fooyin::GuiPluginContext* context, QWidget* parent)
    : FyWidget{parent}
    , m_context{context}
    , m_flipTimer{new QTimer(this)}
    , m_currentFlipIndex{0}
    , m_isFlipping{false}
    , m_flipProgress{0.0f}
{
    setMouseTracking(true); // Enable mouse tracking for tooltips
    connect(m_flipTimer, &QTimer::timeout, this, &AlbumMosaicWidget::flipAnimation);
    m_flipTimer->start(3000); // Flip every 3 seconds
    
    // Timer to check for completed embedded cover extractions
    QTimer* embeddedCheckTimer = new QTimer(this);
    connect(embeddedCheckTimer, &QTimer::timeout, this, &AlbumMosaicWidget::checkEmbeddedCovers);
    embeddedCheckTimer->start(500); // Check every 500ms
    
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
    // Load album metadata from Fooyin database (not the images)
    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/fooyin/fooyin.db";
    
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "fooyin_connection");
    db.setDatabaseName(dbPath);
    
    if(db.open()) {
        QSqlQuery query(db);
        query.exec("SELECT DISTINCT Album, AlbumArtist, FilePath FROM Tracks");
        
        QSet<QString> uniqueAlbums;
        
        while(query.next()) {
            QString album = query.value(0).toString();
            QString albumArtist = query.value(1).toString();
            QString filePath = query.value(2).toString();
            
            QString albumKey = album + "|" + albumArtist;
            if(!uniqueAlbums.contains(albumKey)) {
                uniqueAlbums.insert(albumKey);
                
                QString coverPath;
                
                
                // Find cover path in the same directory as the audio file (external files only)
                QFileInfo fileInfo(filePath);
                QDir dir = fileInfo.absoluteDir();
                
                // Extended list of cover art filenames
                QStringList coverNames = {"cover.jpg", "cover.png", "cover.jpeg", "cover.webp", "cover.gif", "cover.bmp",
                                          "folder.jpg", "folder.png", "folder.jpeg", "folder.webp", "folder.gif", "folder.bmp",
                                          "front.jpg", "front.png", "front.jpeg", "front.webp", "front.gif", "front.bmp",
                                          "album.jpg", "album.png", "album.jpeg", "album.webp", "album.gif", "album.bmp",
                                          "artwork.jpg", "artwork.png", "artwork.jpeg", "artwork.webp", "artwork.gif", "artwork.bmp",
                                          "AlbumArt.jpg", "AlbumArt.png", "AlbumArt.jpeg", "AlbumArt.webp",
                                          "AlbumArtSmall.jpg", "AlbumArtSmall.png",
                                          ".folder.jpg", ".folder.png",
                                          "thumb.jpg", "thumb.png", "thumb.jpeg", "thumb.webp",
                                          "poster.jpg", "poster.png", "poster.jpeg", "poster.webp"};
                
                QStringList imageFilters = {"*.jpg", "*.jpeg", "*.png", "*.webp", "*.gif", "*.bmp", "*.tiff", "*.tif"};
                
                // First try specific cover names in current directory
                for(const QString& coverName : coverNames) {
                    QString path = dir.filePath(coverName);
                    if(QFile::exists(path)) {
                        coverPath = path;
                        break;
                    }
                }
                
                // If no specific cover found, try to find any image file in the directory
                if(coverPath.isEmpty()) {
                    dir.setNameFilters(imageFilters);
                    dir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
                    QFileInfoList imageFiles = dir.entryInfoList();
                    if(!imageFiles.isEmpty()) {
                        coverPath = imageFiles.first().absoluteFilePath();
                    }
                }
                
                // If still no cover, try parent directory (for multi-disc albums)
                if(coverPath.isEmpty()) {
                    QDir parentDir = dir;
                    if(parentDir.cdUp()) {
                        for(const QString& coverName : coverNames) {
                            QString path = parentDir.filePath(coverName);
                            if(QFile::exists(path)) {
                                coverPath = path;
                                break;
                            }
                        }
                        
                        if(coverPath.isEmpty()) {
                            parentDir.setNameFilters(imageFilters);
                            parentDir.setFilter(QDir::Files | QDir::NoDotAndDotDot);
                            QFileInfoList parentImageFiles = parentDir.entryInfoList();
                            if(!parentImageFiles.isEmpty()) {
                                coverPath = parentImageFiles.first().absoluteFilePath();
                            }
                        }
                    }
                }
                
                // Store album metadata (cover path, not loaded image)
                AlbumInfo info;
                info.album = album;
                info.albumArtist = albumArtist;
                info.filePath = filePath;
                info.coverPath = coverPath;
                m_albums.append(info);
            }
        }
        
        db.close();
    }
    
    QSqlDatabase::removeDatabase("fooyin_connection");
    
    // Shuffle albums for more random display
    std::shuffle(m_albums.begin(), m_albums.end(), *QRandomGenerator::global());
    
    qDebug() << "Loaded" << m_albums.size() << "albums from Fooyin library (metadata only, shuffled)";
}

QPixmap AlbumMosaicWidget::loadAlbumCover(const AlbumInfo& album)
{
    QString cacheKey = album.album + "|" + album.albumArtist;
    
    // Check cache first
    if(m_coverCache.contains(cacheKey)) {
        m_lastUsedTime[cacheKey] = QDateTime::currentMSecsSinceEpoch();
        return m_coverCache[cacheKey];
    }
    
    // Check if embedded extraction is already in progress
    if(m_embeddedCoverFutures.contains(cacheKey)) {
        return QPixmap(); // Return placeholder while extracting
    }
    
    // Try external cover file first
    if(!album.coverPath.isEmpty() && QFile::exists(album.coverPath)) {
        QPixmap pixmap(album.coverPath);
        if(!pixmap.isNull()) {
            // Scale to max 256x256
            if(pixmap.width() > 256 || pixmap.height() > 256) {
                pixmap = pixmap.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            }
            m_coverCache[cacheKey] = pixmap;
            m_lastUsedTime[cacheKey] = QDateTime::currentMSecsSinceEpoch();
            cleanupCache();
            return pixmap;
        }
    }
    
    // Only try embedded cover extraction if we haven't already tried
    // Use a marker to avoid retrying albums without embedded covers
    static QSet<QString> noEmbeddedCover;
    if(noEmbeddedCover.contains(cacheKey)) {
        return QPixmap(); // Already tried and failed
    }
    
    // Try to extract embedded cover asynchronously (lazy loading)
    // Return placeholder immediately, load embedded cover in background
    QFuture<QByteArray> future = QtConcurrent::run([album]() -> QByteArray {
        QByteArray coverData;
        
        TagLib::FileRef fileRef(album.filePath.toUtf8().constData());
        if(!fileRef.isNull() && fileRef.file()) {
            // Try MP3
            if(TagLib::MPEG::File* mp3File = dynamic_cast<TagLib::MPEG::File*>(fileRef.file())) {
                if(mp3File->hasID3v2Tag()) {
                    TagLib::ID3v2::Tag* tag = mp3File->ID3v2Tag();
                    TagLib::ID3v2::FrameList frames = tag->frameList("APIC");
                    if(!frames.isEmpty()) {
                        TagLib::ID3v2::AttachedPictureFrame* frame = 
                            static_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
                        if(frame && !frame->picture().isEmpty()) {
                            coverData = QByteArray(frame->picture().data(), frame->picture().size());
                        }
                    }
                }
            }
            // Try FLAC
            else if(TagLib::FLAC::File* flacFile = dynamic_cast<TagLib::FLAC::File*>(fileRef.file())) {
                if(flacFile->hasID3v2Tag()) {
                    TagLib::ID3v2::Tag* tag = flacFile->ID3v2Tag();
                    TagLib::ID3v2::FrameList frames = tag->frameList("APIC");
                    if(!frames.isEmpty()) {
                        TagLib::ID3v2::AttachedPictureFrame* frame = 
                            static_cast<TagLib::ID3v2::AttachedPictureFrame*>(frames.front());
                        if(frame && !frame->picture().isEmpty()) {
                            coverData = QByteArray(frame->picture().data(), frame->picture().size());
                        }
                    }
                }
                if(coverData.isEmpty() && flacFile->pictureList().size() > 0) {
                    TagLib::FLAC::Picture* pic = flacFile->pictureList()[0];
                    if(pic && !pic->data().isEmpty()) {
                        coverData = QByteArray(pic->data().data(), pic->data().size());
                    }
                }
            }
            // Try MP4/M4A
            else if(TagLib::MP4::File* mp4File = dynamic_cast<TagLib::MP4::File*>(fileRef.file())) {
                TagLib::MP4::Item coverItem = mp4File->tag()->item("covr");
                if(coverItem.isValid()) {
                    TagLib::MP4::CoverArtList coverArtList = coverItem.toCoverArtList();
                    if(!coverArtList.isEmpty()) {
                        TagLib::MP4::CoverArt coverArt = coverArtList[0];
                        coverData = QByteArray(coverArt.data().data(), coverArt.data().size());
                    }
                }
            }
        }
        
        return coverData;
    });
    
    // Store the future for later retrieval
    m_embeddedCoverFutures[cacheKey] = future;
    
    // Return placeholder for now
    return QPixmap();
}

void AlbumMosaicWidget::checkEmbeddedCovers()
{
    QStringList completedKeys;
    static QSet<QString> noEmbeddedCover; // Mark albums without embedded covers
    
    for(auto it = m_embeddedCoverFutures.begin(); it != m_embeddedCoverFutures.end(); ++it) {
        const QString& key = it.key();
        QFuture<QByteArray>& future = it.value();
        
        if(future.isFinished()) {
            QByteArray coverData = future.result();
            if(!coverData.isEmpty()) {
                QPixmap pixmap;
                if(pixmap.loadFromData(coverData)) {
                    // Scale to max 256x256
                    if(pixmap.width() > 256 || pixmap.height() > 256) {
                        pixmap = pixmap.scaled(256, 256, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    }
                    m_coverCache[key] = pixmap;
                    m_lastUsedTime[key] = QDateTime::currentMSecsSinceEpoch();
                    cleanupCache();
                    completedKeys.append(key);
                }
            } else {
                // Mark as no cover to avoid retrying
                noEmbeddedCover.insert(key);
                completedKeys.append(key);
            }
        }
    }
    
    // Remove completed futures
    for(const QString& key : completedKeys) {
        m_embeddedCoverFutures.remove(key);
    }
    
    // Repaint if any covers were loaded
    if(!completedKeys.isEmpty()) {
        update();
    }
}

void AlbumMosaicWidget::cleanupCache()
{
    if(m_coverCache.size() <= MAX_CACHE_SIZE) {
        return;
    }
    
    // Find least recently used items
    QList<QPair<QString, qint64>> items;
    for(auto it = m_lastUsedTime.constBegin(); it != m_lastUsedTime.constEnd(); ++it) {
        items.append(qMakePair(it.key(), it.value()));
    }
    
    // Sort by last used time (oldest first)
    std::sort(items.begin(), items.end(), [](const QPair<QString, qint64>& a, const QPair<QString, qint64>& b) {
        return a.second < b.second;
    });
    
    // Remove oldest items until cache size is within limit
    int itemsToRemove = m_coverCache.size() - MAX_CACHE_SIZE;
    for(int i = 0; i < itemsToRemove && i < items.size(); ++i) {
        const QString& path = items[i].first;
        m_coverCache.remove(path);
        m_lastUsedTime.remove(path);
    }
    
    qDebug() << "Cache cleanup: removed" << itemsToRemove << "items, cache size now" << m_coverCache.size();
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
    // Query the database to get all tracks for this album
    QString dbPath = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/fooyin/fooyin.db";
    
    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "fooyin_play_connection");
    db.setDatabaseName(dbPath);
    
    if(db.open()) {
        QSqlQuery query(db);
        query.prepare("SELECT FilePath FROM Tracks WHERE Album = ? AND AlbumArtist = ? ORDER BY TrackNumber");
        query.addBindValue(album);
        query.addBindValue(albumArtist);
        
        if(query.exec()) {
            QStringList filePaths;
            while(query.next()) {
                filePaths.append(query.value(0).toString());
            }
            
            if(!filePaths.isEmpty()) {
                qDebug() << "Playing album:" << album << "by" << albumArtist << "with" << filePaths.size() << "tracks";
                // TrackSelectionController keeps causing crashes even with complete Track objects
                // Alternative: Create a playlist file and ask Fooyin to load it
                QString playlistPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/album_playlist.m3u";
                QFile playlistFile(playlistPath);
                if(playlistFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    QTextStream out(&playlistFile);
                    out << "#EXTM3U\n";
                    for(const QString& path : filePaths) {
                        out << path << "\n";
                    }
                    playlistFile.close();
                    qDebug() << "Created playlist:" << playlistPath;
                    
                    // Try to open the playlist with Fooyin
                    QProcess::startDetached("fooyin", QStringList() << playlistPath);
                }
            }
        }
        
        db.close();
    }
    
    QSqlDatabase::removeDatabase("fooyin_play_connection");
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
                
                // Load cover on demand (lazy loading)
                QPixmap cover = loadAlbumCover(album);
                
                if(!cover.isNull()) {
                    if(m_isFlipping && i == m_currentFlipIndex) {
                        // Apply flip effect
                        painter.save();
                        QTransform transform;
                        transform.translate(cell.center().x(), cell.center().y());
                        transform.scale(m_flipProgress, 1.0f);
                        transform.translate(-cell.center().x(), -cell.center().y());
                        painter.setTransform(transform);
                        painter.drawPixmap(cell, cover);
                        painter.restore();
                    } else {
                        painter.drawPixmap(cell, cover);
                    }
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
