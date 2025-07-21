#include <gtest/gtest.h>
#include <chucho/configuration.hpp>
#include <cstdlib>

int main(int argc, char* argv[])
{
    std::string cnf =
R"(
- chucho::logger:
    name: <root>
    level: debug
    chucho::cout_writer:
        chucho::pattern_formatter:
            pattern: '%c{1}: %m%n'
)";
    auto lvl = std::getenv("CHUCHO_LEVEL");
    if (lvl != nullptr)
        cnf.replace(cnf.find("debug"), 5, lvl);
    chucho::configuration::set_fallback(cnf);
    chucho::configuration::set_file_name("");
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
