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
#include <QSet>
#include <QRect>
#include <QElapsedTimer>
#include <QComboBox>
#include <core/plugins/coreplugincontext.h>
#include <gui/plugins/guiplugin.h>
#include <gui/coverartworktypes.h>
#include <core/track.h>
#include <memory>

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
class TrackSorter;
class ToolTip;
class WidgetContext;
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
    void leaveEvent(QEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void searchEvent(const Fooyin::SearchRequest& request) override;
    void saveLayoutData(QJsonObject& layout) override;
    void loadLayoutData(const QJsonObject& layout) override;
    void finalise() override;
    void openConfigDialog() override;
    void layoutEditingMenu(QMenu* menu) override;

private:
    // Enums (declared first — methods and members below reference them)
    enum class AnimType { Flip3D, Crossfade, Slide, Zoom, PageCurl, Random };
    enum class AnimSpeed { Fast, Medium, Slow };
    enum class AnimScope { Single, Multiple, Wave };
    enum class SortMode { Random, Year, YearDesc, Rating, PlayCount, Recent };

    // Enum <-> settings key helpers (single source of truth for string mapping)
    static QString animTypeKey(AnimType t);
    static AnimType animTypeFromKey(const QString& s);
    static QString animSpeedKey(AnimSpeed s);
    static AnimSpeed animSpeedFromKey(const QString& s);
    static QString animScopeKey(AnimScope s);
    static AnimScope animScopeFromKey(const QString& s);
    static QString sortModeKey(SortMode m);
    static SortMode sortModeFromKey(const QString& s);
    int animIntervalMs() const;

    void loadAlbumMetadata();
    void updateMosaic();
    void triggerAnimation();
    void preloadCovers(int count);
    QList<int> buildSwapCandidates(int maxCount, const QSet<int>& visibleAlbumIndices);
    void skipAnimationBatch(const char* reason);
    void randomizeGrid();
    AnimType effectiveAnimType() const;
    void playAlbum(const QString& album, const QString& albumArtist);
    void showInLibrary(const QString& album, const QString& albumArtist);
    void showSettingsDialog();
    void addQuickSettings(QMenu* menu);
    void loadSettings();
    Fooyin::TrackList getAlbumTracks(const QString& album, const QString& albumArtist);

    Fooyin::GuiPluginContext* m_guiContext;
    Fooyin::CorePluginContext* m_coreContext;
    Fooyin::CoverProvider* m_coverProvider;
    QTimer* m_animTimer;
    QVector<AlbumInfo> m_albums;
    QHash<QString, Fooyin::TrackList> m_albumTracksCache; // Cache: "album|artist" -> tracks
    QHash<QString, int> m_albumKeyToIndex; // "album|artist" -> index in m_albums (O(1) coverAdded lookup)
    QVector<QRect> m_coverPositions;
    QVector<int> m_currentGridIndices;
    QVector<int> m_albumOrder; // Pre-shuffled permutation of album indices

    // Cover cache: maps album index -> scaled pixmap (avoids re-scaling every frame)
    // CoverProvider handles the expensive MP3 decode + thumbnail cache; we only cache the scaling step.
    static constexpr int MAX_CACHE_SIZE = 150; // Base, multiplied by visible count at runtime
    QHash<int, QPixmap> m_scaledCache; // albumIndex -> scaled-to-cell-size pixmap
    QHash<int, int> m_coverFadeProgress; // Fade-in progress per album (0-100)
    QSize m_scaledCacheCellSize{0, 0}; // Cell size when scaled cache was built
    qint64 m_placeholderCacheKey{0}; // For detecting placeholder returns
    QSet<int> m_recentSwappedAlbums; // Avoid swapping back recent albums (anti-flicker)
    QList<int> m_recentSwappedQueue; // FIFO order for bounded eviction of m_recentSwappedAlbums
    static constexpr int MAX_RECENT_SWAPPED = 64; // ~1 full wave batch stays protected
    void markAlbumSwapped(int albumIndex);
    QSet<int> m_pendingPreload; // Albums with an async cover load in flight from the preload loop
    bool m_animRetryPending{false}; // A short retry singleShot is already scheduled after a skipped batch
    int m_consecutiveSkips{0}; // For throttling the skipped-batch diagnostic log
    static constexpr int FADE_STEPS = 8; // Fade-in over 8 frames (~160ms at 20fps)
    QTimer* m_fadeTimer{nullptr}; // Fade-in animation timer
    QTimer* m_toolTipTimer{nullptr}; // Delay before hiding tooltip
    QTimer* m_resizeTimer{nullptr}; // Debounce for resize-triggered cache invalidation
    QTimer* m_searchTimer{nullptr}; // Debounce for search-triggered metadata reload
    void invalidateScaledCache();
    QPixmap getCoverForPaint(int albumIndex, const QSize& cellSize, const Fooyin::ThumbnailSize& coverSize);
    void updateVisibleThumbnailKeys();
    void advanceFade();

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
    int m_playingAlbumIndex{-1}; // Pre-resolved index into m_albums (-1 = not playing/unknown)

    // Search filter
    QString m_searchQuery;

    AnimType m_animType{AnimType::Flip3D};
    AnimSpeed m_animSpeed{AnimSpeed::Medium};
    AnimScope m_animScope{AnimScope::Single};
    SortMode m_sortMode{SortMode::Random};

    // Configurable options
    bool m_enableAnim{true};
    int m_columnCount{10};
    int m_hoveredCellIndex{-1}; // Currently hovered cell
    QString m_genreFilter; // Genre filter (empty = all genres)
    QString m_artistFilter; // Artist filter (empty = all artists)
    QColor m_bgColor{Qt::black}; // Grid background color

    // Inline sort bar
    QComboBox* m_sortCombo{nullptr};
    void onSortChanged();

    // Fooyin styled ToolTip for cover hover (top-level)
    Fooyin::ToolTip* m_toolTip{nullptr};

    // WidgetContext for TrackSelectionController integration
    Fooyin::WidgetContext* m_widgetContext{nullptr};

    // TrackSorter for idiomatic Fooyin sorting
    std::unique_ptr<Fooyin::TrackSorter> m_trackSorter;
};
