cmake_minimum_required(VERSION 3.20)

project(nashville VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_library(CHUCHO_LIB chucho REQUIRED)

qt_add_executable(nashville
    app.cpp
    app.hpp
    bar.hpp
    chord.cpp
    chord.hpp
    main.cpp
    nashville.ui
    title_text.cpp
    title_text.hpp
)

target_link_libraries(nashville PRIVATE Qt6::Widgets ${CHUCHO_LIB})
