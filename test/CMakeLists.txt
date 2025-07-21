enable_testing()

add_executable(unit-test
    ../chord.cpp
    chord.cpp
    main.cpp
)

target_link_libraries(unit-test ${CHUCHO_LIB} GTest::gtest_main)
