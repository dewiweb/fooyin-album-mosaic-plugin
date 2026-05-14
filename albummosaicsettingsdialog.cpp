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
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDialogButtonBox>
#include <QGroupBox>

AlbumMosaicSettingsDialog::AlbumMosaicSettingsDialog(Fooyin::SettingsManager* settingsManager, QWidget* parent)
    : QDialog{parent}
    , m_settingsManager{settingsManager}
    , m_enableFlipCheckbox{new QCheckBox(tr("Enable Flip Animation"), this)}
    , m_flipIntervalSpinBox{new QSpinBox(this)}
    , m_columnCountSpinBox{new QSpinBox(this)}
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

void AlbumMosaicSettingsDialog::loadSettings()
{
    if(!m_settingsManager) {
        return;
    }
    
    m_enableFlipCheckbox->setChecked(m_settingsManager->value(QStringLiteral("AlbumMosaic/EnableFlip")).toBool());
    m_flipIntervalSpinBox->setValue(m_settingsManager->value(QStringLiteral("AlbumMosaic/FlipInterval")).toInt());
    m_columnCountSpinBox->setValue(m_settingsManager->value(QStringLiteral("AlbumMosaic/ColumnCount")).toInt());
}

void AlbumMosaicSettingsDialog::saveSettings()
{
    if(!m_settingsManager) {
        return;
    }
    
    m_settingsManager->set(QStringLiteral("AlbumMosaic/EnableFlip"), m_enableFlipCheckbox->isChecked());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/FlipInterval"), m_flipIntervalSpinBox->value());
    m_settingsManager->set(QStringLiteral("AlbumMosaic/ColumnCount"), m_columnCountSpinBox->value());
    
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
}
