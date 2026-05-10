#include <gtest/gtest.h>
#include "loggable.hpp"
#include "spdlog/cfg/env.h"

int main(int argc, char* argv[])
{
    spdlog::set_pattern(nashville::loggable::PATTERN);
    if (std::getenv(nashville::loggable::ENV_VARIABLE) != nullptr)
        spdlog::cfg::load_env_levels(nashville::loggable::ENV_VARIABLE);
    else
        spdlog::set_level(spdlog::level::debug);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
