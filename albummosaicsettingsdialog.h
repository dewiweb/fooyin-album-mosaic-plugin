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

#include <QDialog>
#include <QCheckBox>
#include <QSpinBox>

class QVBoxLayout;

namespace Fooyin {
class SettingsManager;
}

class AlbumMosaicSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AlbumMosaicSettingsDialog(Fooyin::SettingsManager* settingsManager, QWidget* parent = nullptr);
    ~AlbumMosaicSettingsDialog() override;

private slots:
    void applySettings();
    void restoreDefaults();

private:
    void loadSettings();
    void saveSettings();

    Fooyin::SettingsManager* m_settingsManager;
    QCheckBox* m_enableFlipCheckbox;
    QSpinBox* m_flipIntervalSpinBox;
    QSpinBox* m_columnCountSpinBox;
};
