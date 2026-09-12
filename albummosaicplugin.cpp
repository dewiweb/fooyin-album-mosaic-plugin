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

#include "albummosaicplugin.h"

#include "albummosaicwidget.h"

#include <gui/widgetprovider.h>
#include <gui/coverprovider.h>
#include <utils/settings/settingsmanager.h>

void AlbumMosaicPlugin::initialise(const Fooyin::CorePluginContext& context)
{
    m_core = std::make_unique<Fooyin::CorePluginContext>(context);

    // Create settings for the plugin
    if(context.settingsManager) {
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/EnableAnim"), true);
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/AnimInterval"), 3000);
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/ColumnCount"), 10);
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/GenreFilter"), QString());
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/ArtistFilter"), QString());
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/AnimType"), QStringLiteral("Flip3D"));
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/AnimSpeed"), QStringLiteral("Medium"));
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/AnimScope"), QStringLiteral("Single"));
        context.settingsManager->createSetting(QStringLiteral("AlbumMosaic/BgColor"), QStringLiteral("#000000"));
    }
}

void AlbumMosaicPlugin::initialise(const Fooyin::GuiPluginContext& context)
{
    m_context = const_cast<Fooyin::GuiPluginContext*>(&context);

    // Use the shared CoverRepository from the GUI context (0.12.6+)
    // This shares the cover cache with all other fooyin widgets
    if(context.coverRepository) {
        m_coverProvider = new Fooyin::CoverProvider(context.coverRepository, this);
        m_coverProvider->setUsePlaceholder(false);
    }

    context.widgetProvider->registerWidget("AlbumMosaic", [this]() { return new AlbumMosaicWidget(m_context, m_core.get(), m_coverProvider); }, "Album Mosaic");
}
