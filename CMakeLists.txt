cmake_minimum_required(VERSION 3.20)
project(nashville VERSION 1.0.0 LANGUAGES CXX C)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release FORCE)
endif()

#if (CMAKE_COMPILER_ID STREQUAL Clang AND CMAKE_SYSTEM_NAME STREQUAL Linux)
    #set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")   
#endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(.)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets PrintSupport)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
include_directories(${Qt6Core_INCLUDE_DIRS} ${Qt6Widgets_INCLUDE_DIRS})

find_package(GTest REQUIRED)

qt_add_executable(nashville
    app.cpp
    app.hpp
    database.cpp
    database.hpp
    loggable.cpp
    loggable.hpp
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
    nashville.qrc
    nashville.ui
    sqlite3.c
    sqlite3.h
    view/bar_renderer.cpp
    view/bar_renderer.hpp
    view/chord_renderer.cpp
    view/chord_renderer.hpp
    view/layout_structs.hpp
    view/margin_renderer.cpp
    view/margin_renderer.hpp
    view/song_body_widget.cpp
    view/song_body_widget.hpp
    view/song_widget.cpp
    view/song_widget.hpp
)

target_link_libraries(nashville PRIVATE Qt6::Widgets Qt6::Core Qt6::PrintSupport)

add_subdirectory(test)
