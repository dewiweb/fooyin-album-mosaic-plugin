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

AlbumMosaicSettingsDialog::AlbumMosaicSettingsDialog(Fooyin::SettingsManager* settingsManager, Fooyin::MusicLibrary* library, QWidget* parent)
    : QDialog{parent}
    , m_settingsManager{settingsManager}
    , m_library{library}
    , m_enableFlipCheckbox{new QCheckBox(tr("Enable Flip Animation"), this)}
    , m_flipIntervalSpinBox{new QSpinBox(this)}
    , m_columnCountSpinBox{new QSpinBox(this)}
    , m_genreComboBox{new QComboBox(this)}
    , m_artistComboBox{new QComboBox(this)}
{
    setWindowTitle(tr("Album Mosaic Settings"));
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    mainLayout->setSpacing(10);
    
    // Flip settings group
    auto* flipGroup = new QGroupBox(tr("Flip Animation"), this);
    auto* flipLayout = new QVBoxLayout(flipGroup);
    
    flipLayout->addWidget(m_enableFlipCheckbox);
    
    auto* intervalLayout = new QHBoxLayout();
    intervalLayout->addWidget(new QLabel(tr("Flip Interval (ms):"), this));
    m_flipIntervalSpinBox->setRange(1000, 30000);
    m_flipIntervalSpinBox->setSingleStep(1000);
    intervalLayout->addWidget(m_flipIntervalSpinBox);
    flipLayout->addLayout(intervalLayout);
    
    mainLayout->addWidget(flipGroup);
    
    // Grid settings group
    auto* gridGroup = new QGroupBox(tr("Grid Layout"), this);
    auto* gridLayout = new QVBoxLayout(gridGroup);
    
    auto* columnLayout = new QHBoxLayout();
    columnLayout->addWidget(new QLabel(tr("Number of Columns:"), this));
    m_columnCountSpinBox->setRange(1, 20);
    columnLayout->addWidget(m_columnCountSpinBox);
    gridLayout->addLayout(columnLayout);
    
    mainLayout->addWidget(gridGroup);
    
    // Filter settings group
    auto* filterGroup = new QGroupBox(tr("Filter"), this);
    auto* filterLayout = new QVBoxLayout(filterGroup);
    
    auto* genreLayout = new QHBoxLayout();
    genreLayout->addWidget(new QLabel(tr("Filter by Genre:"), this));
    m_genreComboBox->addItem(tr("All Genres"), QString());
    m_genreComboBox->setEditable(true); // Allow custom genre entry
    genreLayout->addWidget(m_genreComboBox);
    filterLayout->addLayout(genreLayout);
    
    auto* artistLayout = new QHBoxLayout();
    artistLayout->addWidget(new QLabel(tr("Filter by Artist:"), this));
    m_artistComboBox->addItem(tr("All Artists"), QString());
    m_artistComboBox->setEditable(true); // Allow custom artist entry
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
    
    // Populate genre combo box
    QStringList genreList = genres.values();
    std::sort(genreList.begin(), genreList.end(), [](const QString& a, const QString& b) {
        return a.compare(b, Qt::CaseInsensitive) < 0;
    });
    for(const QString& genre : genreList) {
        if(m_genreComboBox->findData(genre) < 0) {
            m_genreComboBox->addItem(genre, genre);
        }
    }
    
    // Populate artist combo box
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
    
    m_enableFlipCheckbox->setChecked(m_settingsManager->value(QStringLiteral("AlbumMosaic/EnableFlip")).toBool());
    m_flipIntervalSpinBox->setValue(m_settingsManager->value(QStringLiteral("AlbumMosaic/FlipInterval")).toInt());
    m_columnCountSpinBox->setValue(m_settingsManager->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt());
    
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
}

void AlbumMosaicSettingsDialog::saveSettings()
{
    if(!m_settingsManager) {
        return;
    }
    
    m_settingsManager->set(QStringLiteral("AlbumMosaic/EnableFlip"), m_enableFlipCheckbox->isChecked());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/FlipInterval"), m_flipIntervalSpinBox->value());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/ColumnCount"), m_columnCountSpinBox->value());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/GenreFilter"), m_genreComboBox->currentData().toString());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/ArtistFilter"), m_artistComboBox->currentData().toString());
    
    m_settingsManager->storeSettings();
}

void AlbumMosaicSettingsDialog::applySettings()
{
    saveSettings();
    accept();
}

void AlbumMosaicSettingsDialog::restoreDefaults()
{
    m_enableFlipCheckbox->setChecked(true);
    m_flipIntervalSpinBox->setValue(3000);
    m_columnCountSpinBox->setValue(10);
    m_genreComboBox->setCurrentIndex(0); // All Genres
    m_artistComboBox->setCurrentIndex(0); // All Artists
}
