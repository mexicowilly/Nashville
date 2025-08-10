#include <gtest/gtest.h>
#include "../model/bar.hpp"

using namespace nashville;

TEST(bar, user_input)
{
    model::bar b;
    EXPECT_NO_THROW(b.parse_user_input("1"));
    EXPECT_EQ(1, b.chords().size());
    EXPECT_NO_THROW(b.parse_user_input("1 4"));
    EXPECT_EQ(2, b.chords().size());
    EXPECT_NO_THROW(b.parse_user_input("    1             4    7"));
    EXPECT_EQ(3, b.chords().size());
    EXPECT_NO_THROW(b.parse_user_input("5:q 4:q 3:q 4:q"));
    EXPECT_EQ(4, b.chords().size());
}
