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
#include <QComboBox>
#include <QLineEdit>
#include <QPushButton>

class QVBoxLayout;

namespace Fooyin {
class SettingsManager;
class MusicLibrary;
}

class AlbumMosaicSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AlbumMosaicSettingsDialog(Fooyin::SettingsManager* settingsManager, Fooyin::MusicLibrary* library, QWidget* parent = nullptr);
    ~AlbumMosaicSettingsDialog() override;

private slots:
    void applySettings();
    void restoreDefaults();

private:
    void loadSettings();
    void saveSettings();
    void populateFiltersFromLibrary();
    void updateBgColorButton();

    Fooyin::SettingsManager* m_settingsManager;
    Fooyin::MusicLibrary* m_library;
    QCheckBox* m_enableAnimCheckbox;
    QSpinBox* m_columnCountSpinBox;
    QComboBox* m_genreComboBox;
    QComboBox* m_artistComboBox;
    QComboBox* m_animTypeComboBox;
    QComboBox* m_animSpeedComboBox;
    QComboBox* m_animScopeComboBox;
    QComboBox* m_sortModeComboBox;
    QPushButton* m_bgColorButton;
    QColor m_bgColor;
};
