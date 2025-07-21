#include <gtest/gtest.h>
#include "../chord.hpp"

using namespace nashville;

TEST(chord, user_input)
{
    model::chord c;
    EXPECT_NO_THROW(c.parse_user_input("1"));
    model::chord ref;
    EXPECT_EQ(c, ref);
}
