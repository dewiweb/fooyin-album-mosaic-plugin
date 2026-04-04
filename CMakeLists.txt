cmake_minimum_required(VERSION 3.14)

project(
    albummosaicplugin
    VERSION 1.0.0
    DESCRIPTION "Album Mosaic Plugin for Fooyin - iTunes style album art display"
    HOMEPAGE_URL "https://github.com/yourusername/albums_background"
    LANGUAGES CXX
)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(Fooyin)

create_fooyin_plugin(
    albummosaicplugin
    DEPENDS Fooyin::Gui Fooyin::Core
    SOURCES albummosaicplugin.cpp
            albummosaicplugin.h
            albummosaicwidget.cpp
            albummosaicwidget.h
)
