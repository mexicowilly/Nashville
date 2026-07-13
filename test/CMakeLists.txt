enable_testing()

find_package(GTest REQUIRED)

add_executable(unit-test
    ../model/annotations.cpp
    ../model/annotations.hpp
    ../model/bar.cpp
    ../model/chord.cpp
    ../model/song.cpp
    ../model/time_signature.cpp
    ../database.cpp
    ../sqlite3.c
    ../loggable.cpp
    ../loggable.hpp
    annotations.cpp
    bar.cpp
    chord.cpp
    database.cpp
    main.cpp
    page_geometry.cpp
    time_signature.cpp
)

target_compile_definitions(unit-test PRIVATE TESTING)

target_link_libraries(unit-test PRIVATE GTest::gtest_main Qt6::Widgets Qt6::Core)
