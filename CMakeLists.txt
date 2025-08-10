cmake_minimum_required(VERSION 3.20)

project(nashville VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
    model/bar.cpp
    model/bar.hpp
    model/chord.cpp
    model/chord.hpp
    main.cpp
    nashville.ui
    title_text.cpp
    title_text.hpp
    widgets/bar.cpp
    widgets/bar.hpp
    widgets/chord.cpp
    widgets/chord.hpp
)

target_link_libraries(nashville PRIVATE ${CHUCHO_LIB} Qt6::Widgets Qt6::Core)

add_subdirectory(test)
