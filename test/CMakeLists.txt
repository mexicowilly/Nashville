enable_testing()

add_executable(unit-test
    ../model/bar.cpp
    ../model/chord.cpp
    ../model/song.cpp
    ../model/time_signature.cpp
    ../database.cpp
    ../sqlite3.c
    bar.cpp
    chord.cpp
    database.cpp
    main.cpp
    time_signature.cpp
)

target_link_libraries(unit-test PRIVATE ${CHUCHO_LIB} GTest::gtest_main Qt6::Widgets Qt6::Core)
