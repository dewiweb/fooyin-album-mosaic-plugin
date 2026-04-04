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

void AlbumMosaicPlugin::initialise(const Fooyin::CorePluginContext& context)
{
    m_core = std::make_unique<Fooyin::CorePluginContext>(context);
    // Create CoverProvider with AudioLoader and SettingsManager
    // Note: CoverProvider has a static cache, so all instances share the same cache
    if(context.audioLoader && context.settingsManager) {
        m_coverProvider = new Fooyin::CoverProvider(context.audioLoader, context.settingsManager, this);
        m_coverProvider->setUsePlaceholder(false); // Don't use placeholder covers
    }
}

void AlbumMosaicPlugin::initialise(const Fooyin::GuiPluginContext& context)
{
    m_context = const_cast<Fooyin::GuiPluginContext*>(&context);
    context.widgetProvider->registerWidget("AlbumMosaic", [this]() { return new AlbumMosaicWidget(m_context, m_core.get(), m_coverProvider); }, "Album Mosaic");
}
