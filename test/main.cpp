#include <gtest/gtest.h>
#include "spdlog/spdlog.h"

int main(int argc, char* argv[])
{
    spdlog::set_pattern("%Y-%m-%d %H:%M:%S.%f %L %n: %v");
    spdlog::set_level(spdlog::level::debug);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
