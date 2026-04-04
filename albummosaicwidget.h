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

#pragma once

#include <gui/fywidget.h>

#include <QPixmap>
#include <QFuture>
#include <QVector>
#include <QMap>
#include <QRect>
#include <gui/plugins/guiplugin.h>

struct AlbumInfo {
    QString album;
    QString albumArtist;
    QString filePath;
    QString coverPath; // Path to cover file, not the loaded image
};

namespace Fooyin {
class GuiPluginContext;
}

class AlbumMosaicWidget : public Fooyin::FyWidget
{
    Q_OBJECT

public:
    explicit AlbumMosaicWidget(Fooyin::GuiPluginContext* context, QWidget* parent = nullptr);
    ~AlbumMosaicWidget() override;

    QString name() const override;
    QString layoutName() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void loadAlbumMetadata();
    QPixmap loadAlbumCover(const AlbumInfo& album);
    void cleanupCache();
    void updateMosaic();
    void flipAnimation();
    void randomizeGrid();
    void playAlbum(const QString& album, const QString& albumArtist);
    void checkEmbeddedCovers();

    Fooyin::GuiPluginContext* m_context;
    QTimer* m_flipTimer;
    QVector<AlbumInfo> m_albums;
    QMap<QString, QPixmap> m_coverCache; // Cache of loaded covers
    QMap<QString, qint64> m_lastUsedTime; // Track when each cover was last used
    QMap<QString, QFuture<QByteArray>> m_embeddedCoverFutures; // Async embedded cover extraction
    static constexpr int MAX_CACHE_SIZE = 200; // Maximum number of covers to keep in cache
    QVector<QRect> m_coverPositions;
    QVector<int> m_currentGridIndices;
    int m_currentFlipIndex;
    bool m_isFlipping;
    float m_flipProgress;
    int m_scrollOffset{0}; // Scroll offset for grid
    int m_totalRows{0}; // Total number of rows in grid
};
