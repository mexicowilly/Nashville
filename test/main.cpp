#include <gtest/gtest.h>
#include <chucho/configuration.hpp>

int main(int argc, char* argv[])
{
    const char* cnf = R"(
- chucho::logger:
    name: <root>
    level: debug
    chucho::cout_writer:
        chucho::pattern_formatter:
            pattern: '%c{1}: %m%n')";
    chucho::configuration::set_fallback(cnf);
    chucho::configuration::set_file_name("");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
