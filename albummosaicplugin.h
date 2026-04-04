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

#include <core/plugins/coreplugin.h>
#include <core/plugins/plugin.h>
#include <gui/plugins/guiplugin.h>

namespace Fooyin {
class CorePluginContext;
class GuiPluginContext;
class CoverProvider;
}

class AlbumMosaicPlugin : public QObject,
                         public Fooyin::Plugin,
                         public Fooyin::CorePlugin,
                         public Fooyin::GuiPlugin
{
public:
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.fooyin.fooyin.plugin/1.0" FILE "metadata.json")
    Q_INTERFACES(Fooyin::Plugin Fooyin::CorePlugin Fooyin::GuiPlugin)

    void initialise(const Fooyin::CorePluginContext& context) override;
    void initialise(const Fooyin::GuiPluginContext& context) override;

private:
    std::unique_ptr<Fooyin::CorePluginContext> m_core;
    Fooyin::GuiPluginContext* m_context{nullptr};
    Fooyin::CoverProvider* m_coverProvider{nullptr};
};
