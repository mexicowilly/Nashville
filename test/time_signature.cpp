#include <gtest/gtest.h>
#include "../model/time_signature.hpp"

using namespace nashville;

TEST(time_signature, user_input)
{
    model::time_signature ts;
    model::time_signature ref;
    ts.parse_user_input("4/4");
    EXPECT_EQ(ts, ref);
    ts.parse_user_input("72/4");
    ref.count(72);
    EXPECT_EQ(ts, ref);
    ts.parse_user_input("7/2");
    ref.count(7).kind(model::time_signature::beat_type::HALF);
    EXPECT_EQ(ts, ref);
    ts.parse_user_input("8/4");
    ref.count(8).kind(model::time_signature::beat_type::QUARTER);
    EXPECT_EQ(ts, ref);
    ts.parse_user_input("9/8");
    ref.count(9).kind(model::time_signature::beat_type::EIGHTH);
    EXPECT_EQ(ts, ref);
    ts.parse_user_input("  4             /4              ");
    ref.count(4).kind(model::time_signature::beat_type::QUARTER);
    EXPECT_EQ(ts, ref);
    ts.parse_user_input("7/ 8              ");
    ref.count(7).kind(model::time_signature::beat_type::EIGHTH);
    EXPECT_EQ(ts, ref);
    EXPECT_THROW(ts.parse_user_input("7/3"), std::invalid_argument);
    EXPECT_THROW(ts.parse_user_input("4/800"), std::invalid_argument);
}
