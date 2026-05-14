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

#pragma once

#include <gui/fywidget.h>

#include <QPixmap>
#include <QVector>
#include <QMap>
#include <QRect>
#include <QFutureWatcher>
#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugin.h>
#include <core/track.h>

struct AlbumInfo {
    QString album;
    QString albumArtist;
    QString filePath;
    QString coverPath; // Path to cover file, not the loaded image
    Fooyin::Track track; // Store track reference for CoverProvider
};

namespace Fooyin {
class GuiPluginContext;
class CorePluginContext;
class CoverProvider;
}

class AlbumMosaicWidget : public Fooyin::FyWidget
{
    Q_OBJECT

public:
    explicit AlbumMosaicWidget(Fooyin::GuiPluginContext* guiContext, Fooyin::CorePluginContext* coreContext, Fooyin::CoverProvider* coverProvider, QWidget* parent = nullptr);
    ~AlbumMosaicWidget() override;

    QString name() const override;
    QString layoutName() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;

private:
    void loadAlbumMetadata();
    void updateMosaic();
    void flipAnimation();
    void randomizeGrid();
    void playAlbum(const QString& album, const QString& albumArtist);
    void queueAlbum(const QString& album, const QString& albumArtist);
    void showAlbumInfo(const AlbumInfo& album);
    void showSettingsDialog();
    void loadSettings();

    Fooyin::GuiPluginContext* m_guiContext;
    Fooyin::CorePluginContext* m_coreContext;
    Fooyin::CoverProvider* m_coverProvider;
    QTimer* m_flipTimer;
    QVector<AlbumInfo> m_albums;
    QVector<QRect> m_coverPositions;
    QVector<int> m_currentGridIndices;
    int m_currentFlipIndex;
    bool m_isFlipping;
    float m_flipProgress;
    int m_flipDirection{1}; // 1 or -1 for flip direction
    int m_oldAlbumIndex{-1}; // Album being flipped away
    int m_newAlbumIndex{-1}; // Album being flipped in
    int m_scrollOffset{0}; // Scroll offset for grid
    int m_totalRows{0}; // Total number of rows in grid
    
    // Configurable options
    bool m_enableFlip{true};
    int m_flipInterval{3000}; // milliseconds
    int m_columnCount{10};
    int m_hoveredCellIndex{-1}; // Currently hovered cell
    int m_rightClickedCellIndex{-1}; // Currently right-clicked cell
};
