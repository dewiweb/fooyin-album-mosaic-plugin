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

#include "albummosaicsettingsdialog.h"

#include <utils/settings/settingsmanager.h>
#include <core/library/musiclibrary.h>
#include <core/track.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QComboBox>
#include <QLineEdit>
#include <QColorDialog>

AlbumMosaicSettingsDialog::AlbumMosaicSettingsDialog(Fooyin::SettingsManager* settingsManager, Fooyin::MusicLibrary* library, QWidget* parent)
    : QDialog{parent}
    , m_settingsManager{settingsManager}
    , m_library{library}
    , m_enableAnimCheckbox{new QCheckBox(tr("Enable Animation"), this)}
    , m_columnCountSpinBox{new QSpinBox(this)}
    , m_genreComboBox{new QComboBox(this)}
    , m_artistComboBox{new QComboBox(this)}
    , m_animTypeComboBox{new QComboBox(this)}
    , m_animSpeedComboBox{new QComboBox(this)}
    , m_animScopeComboBox{new QComboBox(this)}
    , m_sortModeComboBox{new QComboBox(this)}
    , m_displayModeComboBox{new QComboBox(this)}
    , m_autoDownloadCheckbox{new QCheckBox(tr("Auto-download missing artist covers"), this)}
    , m_bgColorButton{new QPushButton(this)}
    , m_bgColor{Qt::black}
{
    setWindowTitle(tr("Album Mosaic Settings"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);

    // Display mode group
    auto* modeGroup = new QGroupBox(tr("Display Mode"), this);
    auto* modeLayout = new QVBoxLayout(modeGroup);

    auto* modeComboLayout = new QHBoxLayout();
    modeComboLayout->addWidget(new QLabel(tr("Cover Type:"), this));
    m_displayModeComboBox->addItem(tr("Album Covers"), QStringLiteral("Album"));
    m_displayModeComboBox->addItem(tr("Artist Covers"), QStringLiteral("Artist"));
    modeComboLayout->addWidget(m_displayModeComboBox);
    modeLayout->addLayout(modeComboLayout);

    modeLayout->addWidget(m_autoDownloadCheckbox);

    mainLayout->addWidget(modeGroup);

    // Animation settings group
    auto* animGroup = new QGroupBox(tr("Animation"), this);
    auto* animLayout = new QVBoxLayout(animGroup);

    animLayout->addWidget(m_enableAnimCheckbox);

    auto* animTypeLayout = new QHBoxLayout();
    animTypeLayout->addWidget(new QLabel(tr("Animation Type:"), this));
    m_animTypeComboBox->addItem(tr("3D Flip"), QStringLiteral("Flip3D"));
    m_animTypeComboBox->addItem(tr("Crossfade"), QStringLiteral("Crossfade"));
    m_animTypeComboBox->addItem(tr("Slide"), QStringLiteral("Slide"));
    m_animTypeComboBox->addItem(tr("Zoom"), QStringLiteral("Zoom"));
    m_animTypeComboBox->addItem(tr("Page Curl"), QStringLiteral("PageCurl"));
    m_animTypeComboBox->addItem(tr("Random"), QStringLiteral("Random"));
    animTypeLayout->addWidget(m_animTypeComboBox);
    animLayout->addLayout(animTypeLayout);

    auto* animSpeedLayout = new QHBoxLayout();
    animSpeedLayout->addWidget(new QLabel(tr("Animation Speed:"), this));
    m_animSpeedComboBox->addItem(tr("Fast"), QStringLiteral("Fast"));
    m_animSpeedComboBox->addItem(tr("Medium"), QStringLiteral("Medium"));
    m_animSpeedComboBox->addItem(tr("Slow"), QStringLiteral("Slow"));
    animSpeedLayout->addWidget(m_animSpeedComboBox);
    animLayout->addLayout(animSpeedLayout);

    auto* animScopeLayout = new QHBoxLayout();
    animScopeLayout->addWidget(new QLabel(tr("Animation Scope:"), this));
    m_animScopeComboBox->addItem(tr("Single Cell"), QStringLiteral("Single"));
    m_animScopeComboBox->addItem(tr("Multiple Cells"), QStringLiteral("Multiple"));
    m_animScopeComboBox->addItem(tr("Wave"), QStringLiteral("Wave"));
    animScopeLayout->addWidget(m_animScopeComboBox);
    animLayout->addLayout(animScopeLayout);

    mainLayout->addWidget(animGroup);

    // Grid settings group
    auto* gridGroup = new QGroupBox(tr("Grid Layout"), this);
    auto* gridLayout = new QVBoxLayout(gridGroup);

    auto* columnLayout = new QHBoxLayout();
    columnLayout->addWidget(new QLabel(tr("Number of Columns:"), this));
    m_columnCountSpinBox->setRange(1, 20);
    columnLayout->addWidget(m_columnCountSpinBox);
    gridLayout->addLayout(columnLayout);

    auto* sortLayout = new QHBoxLayout();
    sortLayout->addWidget(new QLabel(tr("Sort By:"), this));
    m_sortModeComboBox->addItem(tr("Random"), QStringLiteral("Random"));
    m_sortModeComboBox->addItem(tr("Alphabetical"), QStringLiteral("Alphabetical"));
    m_sortModeComboBox->addItem(tr("Year (newest first)"), QStringLiteral("YearDesc"));
    m_sortModeComboBox->addItem(tr("Year (oldest first)"), QStringLiteral("Year"));
    m_sortModeComboBox->addItem(tr("Rating (highest first)"), QStringLiteral("Rating"));
    m_sortModeComboBox->addItem(tr("Play Count (most played)"), QStringLiteral("PlayCount"));
    m_sortModeComboBox->addItem(tr("Recently Played"), QStringLiteral("Recent"));
    sortLayout->addWidget(m_sortModeComboBox);
    gridLayout->addLayout(sortLayout);

    mainLayout->addWidget(gridGroup);

    // Background color group
    auto* bgGroup = new QGroupBox(tr("Background"), this);
    auto* bgLayout = new QHBoxLayout(bgGroup);
    bgLayout->addWidget(new QLabel(tr("Grid Background Color:"), this));
    m_bgColorButton->setMinimumWidth(80);
    bgLayout->addWidget(m_bgColorButton);
    bgLayout->addStretch();
    mainLayout->addWidget(bgGroup);

    connect(m_bgColorButton, &QPushButton::clicked, this, [this]() {
        const QColor chosen = QColorDialog::getColor(m_bgColor, this, tr("Choose Background Color"));
        if(chosen.isValid()) {
            m_bgColor = chosen;
            updateBgColorButton();
        }
    });

    // Filter settings group
    auto* filterGroup = new QGroupBox(tr("Filter"), this);
    auto* filterLayout = new QVBoxLayout(filterGroup);

    auto* genreLayout = new QHBoxLayout();
    genreLayout->addWidget(new QLabel(tr("Filter by Genre:"), this));
    m_genreComboBox->addItem(tr("All Genres"), QString());
    m_genreComboBox->setEditable(true);
    genreLayout->addWidget(m_genreComboBox);
    filterLayout->addLayout(genreLayout);

    auto* artistLayout = new QHBoxLayout();
    artistLayout->addWidget(new QLabel(tr("Filter by Artist:"), this));
    m_artistComboBox->addItem(tr("All Artists"), QString());
    m_artistComboBox->setEditable(true);
    artistLayout->addWidget(m_artistComboBox);
    filterLayout->addLayout(artistLayout);

    mainLayout->addWidget(filterGroup);

    // Populate filters from library data
    populateFiltersFromLibrary();

    // Button box
    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults, this);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &AlbumMosaicSettingsDialog::applySettings);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttonBox, &QDialogButtonBox::clicked, this, [this, buttonBox](QAbstractButton* button) {
        if(buttonBox->buttonRole(button) == QDialogButtonBox::ResetRole) {
            restoreDefaults();
        }
    });

    loadSettings();
}

AlbumMosaicSettingsDialog::~AlbumMosaicSettingsDialog() = default;

void AlbumMosaicSettingsDialog::updateBgColorButton()
{
    // Show the color as the button's background, with hex code as text
    m_bgColorButton->setText(m_bgColor.name());
    m_bgColorButton->setStyleSheet(QStringLiteral("background-color: %1; color: %2;")
        .arg(m_bgColor.name())
        .arg(m_bgColor.lightness() > 128 ? "black" : "white"));
}

void AlbumMosaicSettingsDialog::populateFiltersFromLibrary()
{
    if(!m_library) {
        return;
    }

    Fooyin::TrackList tracks = m_library->tracks();
    QSet<QString> genres;
    QSet<QString> artists;

    for(const Fooyin::Track& track : tracks) {
        if(track.hasGenres()) {
            for(const QString& genre : track.genres()) {
                genres.insert(genre);
            }
        }
        if(!track.albumArtist().isEmpty()) {
            artists.insert(track.albumArtist());
        }
        if(!track.artist().isEmpty()) {
            artists.insert(track.artist());
        }
    }

    QStringList genreList = genres.values();
    std::sort(genreList.begin(), genreList.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for(const QString& genre : genreList) {
        if(m_genreComboBox->findData(genre) < 0) {
            m_genreComboBox->addItem(genre, genre);
        }
    }

    QStringList artistList = artists.values();
    std::sort(artistList.begin(), artistList.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for(const QString& artist : artistList) {
        if(m_artistComboBox->findData(artist) < 0) {
            m_artistComboBox->addItem(artist, artist);
        }
    }
}

void AlbumMosaicSettingsDialog::loadSettings()
{
    if(!m_settingsManager) {
        return;
    }

    m_enableAnimCheckbox->setChecked(m_settingsManager->value(QStringLiteral("AlbumMosaic/EnableAnim")).toBool());
    m_columnCountSpinBox->setValue(m_settingsManager->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt());

    QString displayMode = m_settingsManager->value(QStringLiteral("AlbumMosaic/DisplayMode")).toString();
    int modeIndex = m_displayModeComboBox->findData(displayMode);
    if(modeIndex >= 0) {
        m_displayModeComboBox->setCurrentIndex(modeIndex);
    }
    m_autoDownloadCheckbox->setChecked(m_settingsManager->value(QStringLiteral("AlbumMosaic/AutoDownloadArtistCovers")).toBool());

    QString genreFilter = m_settingsManager->value(QStringLiteral("AlbumMosaic/GenreFilter")).toString();
    int index = m_genreComboBox->findData(genreFilter);
    if(index >= 0) {
        m_genreComboBox->setCurrentIndex(index);
    } else if(!genreFilter.isEmpty()) {
        m_genreComboBox->addItem(genreFilter, genreFilter);
        m_genreComboBox->setCurrentIndex(m_genreComboBox->count() - 1);
    }

    QString artistFilter = m_settingsManager->value(QStringLiteral("AlbumMosaic/ArtistFilter")).toString();
    index = m_artistComboBox->findData(artistFilter);
    if(index >= 0) {
        m_artistComboBox->setCurrentIndex(index);
    } else if(!artistFilter.isEmpty()) {
        m_artistComboBox->addItem(artistFilter, artistFilter);
        m_artistComboBox->setCurrentIndex(m_artistComboBox->count() - 1);
    }

    QString animType = m_settingsManager->value(QStringLiteral("AlbumMosaic/AnimType")).toString();
    index = m_animTypeComboBox->findData(animType);
    if(index >= 0) {
        m_animTypeComboBox->setCurrentIndex(index);
    }

    QString animSpeed = m_settingsManager->value(QStringLiteral("AlbumMosaic/AnimSpeed")).toString();
    index = m_animSpeedComboBox->findData(animSpeed);
    if(index >= 0) {
        m_animSpeedComboBox->setCurrentIndex(index);
    }

    QString animScope = m_settingsManager->value(QStringLiteral("AlbumMosaic/AnimScope")).toString();
    index = m_animScopeComboBox->findData(animScope);
    if(index >= 0) {
        m_animScopeComboBox->setCurrentIndex(index);
    }

    m_bgColor = QColor(m_settingsManager->value(QStringLiteral("AlbumMosaic/BgColor")).toString());
    if(!m_bgColor.isValid()) m_bgColor = Qt::black;
    updateBgColorButton();

    QString sortMode = m_settingsManager->value(QStringLiteral("AlbumMosaic/SortMode")).toString();
    index = m_sortModeComboBox->findData(sortMode);
    if(index >= 0) {
        m_sortModeComboBox->setCurrentIndex(index);
    }
}

void AlbumMosaicSettingsDialog::saveSettings()
{
    if(!m_settingsManager) {
        return;
    }

    m_settingsManager->set(QStringLiteral("AlbumMosaic/EnableAnim"), m_enableAnimCheckbox->isChecked());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/ColumnCount"), m_columnCountSpinBox->value());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/GenreFilter"), m_genreComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/ArtistFilter"), m_artistComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/AnimType"), m_animTypeComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/AnimSpeed"), m_animSpeedComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/AnimScope"), m_animScopeComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/BgColor"), m_bgColor.name());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/SortMode"), m_sortModeComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/DisplayMode"), m_displayModeComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/AutoDownloadArtistCovers"), m_autoDownloadCheckbox->isChecked());

    m_settingsManager->storeSettings();
}

void AlbumMosaicSettingsDialog::applySettings()
{
    saveSettings();
    accept();
}

void AlbumMosaicSettingsDialog::restoreDefaults()
{
    m_enableAnimCheckbox->setChecked(true);
    m_columnCountSpinBox->setValue(10);
    m_genreComboBox->setCurrentIndex(0);
    m_artistComboBox->setCurrentIndex(0);
    m_animTypeComboBox->setCurrentIndex(0);
    m_animSpeedComboBox->setCurrentIndex(1); // Medium
    m_animScopeComboBox->setCurrentIndex(0); // Single
    m_sortModeComboBox->setCurrentIndex(0); // Random
    m_displayModeComboBox->setCurrentIndex(0); // Album
    m_autoDownloadCheckbox->setChecked(false);
    m_bgColor = Qt::black;
    updateBgColorButton();
}
