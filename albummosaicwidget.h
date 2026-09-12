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
#include <QHash>
#include <QRect>
#include <QElapsedTimer>
#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugin.h>
#include <core/track.h>

struct AlbumInfo {
    QString album;
    QString albumArtist;
    QString filePath;
    Fooyin::Track track; // Store track reference for CoverProvider
};

namespace Fooyin {
class GuiPluginContext;
class CorePluginContext;
class CoverProvider;
class PlaylistHandler;
class PlayerController;
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
    void searchEvent(const Fooyin::SearchRequest& request) override;
    void saveLayoutData(QJsonObject& layout) override;
    void loadLayoutData(const QJsonObject& layout) override;

private:
    // Animation type (must be declared before methods that use it)
    enum class AnimType { Flip3D, Crossfade, Slide, Zoom, PageCurl, Random };

    void loadAlbumMetadata();
    void updateMosaic();
    void triggerAnimation();
    void randomizeGrid();
    AnimType effectiveAnimType() const;
    void playAlbum(const QString& album, const QString& albumArtist);
    void queueAlbum(const QString& album, const QString& albumArtist);
    void showAlbumInfo(const AlbumInfo& album);
    void showSettingsDialog();
    void loadSettings();
    Fooyin::TrackList getAlbumTracks(const QString& album, const QString& albumArtist);
    int findAlbumCell(const QString& album, const QString& albumArtist) const;

    Fooyin::GuiPluginContext* m_guiContext;
    Fooyin::CorePluginContext* m_coreContext;
    Fooyin::CoverProvider* m_coverProvider;
    QTimer* m_animTimer;
    QVector<AlbumInfo> m_albums;
    QHash<QString, Fooyin::TrackList> m_albumTracksCache; // Cache: "album|artist" -> tracks
    QVector<QRect> m_coverPositions;
    QVector<int> m_currentGridIndices;
    QVector<int> m_albumOrder; // Pre-shuffled permutation of album indices

    // Multi-cell animation support
    struct ActiveAnim {
        int cellIndex{-1};
        int orderIndex{-1};
        int oldAlbumIndex{-1};
        int newAlbumIndex{-1};
        int swapWithOrderIndex{-1};
        bool newCoverReady{false};
        QElapsedTimer elapsed;
        int delayMs{0}; // Staggered start delay
        AnimType animType{AnimType::Flip3D}; // Resolved type (for Random mode)
    };
    QVector<ActiveAnim> m_activeAnims;

    int m_scrollOffset{0}; // Scroll offset for grid

    // Currently playing album highlight
    QString m_currentPlayingAlbum;
    QString m_currentPlayingArtist;

    // Search filter
    QString m_searchQuery;

    AnimType m_animType{AnimType::Flip3D};

    // Animation speed
    enum class AnimSpeed { Fast, Medium, Slow };
    AnimSpeed m_animSpeed{AnimSpeed::Medium};

    // Animation scope (how many cells animate at once)
    enum class AnimScope { Single, Multiple, Wave };
    AnimScope m_animScope{AnimScope::Single};

    // Configurable options
    bool m_enableAnim{true};
    int m_animInterval{3000}; // milliseconds between animation triggers
    int m_columnCount{10};
    int m_hoveredCellIndex{-1}; // Currently hovered cell
    int m_rightClickedCellIndex{-1}; // Currently right-clicked cell
    QString m_genreFilter; // Genre filter (empty = all genres)
    QString m_artistFilter; // Artist filter (empty = all artists)
    QColor m_bgColor{Qt::black}; // Grid background color
};
