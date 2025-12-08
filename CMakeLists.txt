cmake_minimum_required(VERSION 3.20)

project(nashville VERSION 1.0.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
include_directories(${Qt6Core_INCLUDE_DIRS} ${Qt6Widgets_INCLUDE_DIRS})

find_library(CHUCHO_LIB chucho REQUIRED)

find_package(GTest REQUIRED)

qt_add_executable(nashville
    app.cpp
    app.hpp
    database.cpp
    database.hpp
    measures.cpp
    measures.hpp
    model/bar.cpp
    model/bar.hpp
    model/chord.cpp
    model/chord.hpp
    model/playlist.hpp
    model/song.cpp
    model/song.hpp
    model/time_signature.cpp
    model/time_signature.hpp
    main.cpp
    nashville.ui
    sqlite3.c
    sqlite3.h
    widgets/bar.cpp
    widgets/bar.hpp
    widgets/bar_placeholder.cpp
    widgets/bar_placeholder.hpp
    widgets/chord.cpp
    widgets/chord.hpp
    widgets/key_column.cpp
    widgets/key_column.hpp
    widgets/song.cpp
    widgets/song.hpp
    widgets/title_text.cpp
    widgets/title_text.hpp
)

target_link_libraries(nashville PRIVATE ${CHUCHO_LIB} Qt6::Widgets Qt6::Core)

add_subdirectory(test)
