enable_testing()

add_executable(unit-test
    ../model/chord.cpp
    chord.cpp
    main.cpp
)

target_link_libraries(unit-test PRIVATE ${CHUCHO_LIB} GTest::gtest_main Qt6::Widgets Qt6::Core)
