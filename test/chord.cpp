#include <gtest/gtest.h>
#include "../chord.hpp"

TEST(chord, user_input)
{
    nashville::model::chord c;
    EXPECT_NO_THROW(c.parse_user_input("1"));
    nashville::model::chord ref;
    EXPECT_EQ(c, ref);
}
