#include <gtest/gtest.h>
#include "../model/song.hpp"

using namespace nashville;

TEST(song, voltas)
{
    model::song s("doggies");
    s.add_bar();
    s.add_bar().add_volta(0);
    s.add_bar().add_volta(1);
    auto vs = s.voltas();
    ASSERT_EQ(2, vs.size());
    ASSERT_EQ(1, vs[0].size());
    EXPECT_EQ(1, vs[0][0]);
    ASSERT_EQ(1, vs[1].size());
    EXPECT_EQ(2, vs[1][0]);
    model::song s2("bleh");
    s2.add_bar();
    s2.add_bar().add_volta(0).add_volta(2);
    s2.add_bar().add_volta(1);
    vs = s2.voltas();
    ASSERT_EQ(3, vs.size());
    ASSERT_EQ(1, vs[0].size());
    EXPECT_EQ(1, vs[0][0]);
    ASSERT_EQ(1, vs[1].size());
    EXPECT_EQ(2, vs[1][0]);
    ASSERT_EQ(1, vs[2].size());
    EXPECT_EQ(1, vs[2][0]);
    model::song s3("Hunky monkey");
    s3.add_bar().add_volta(0);
    s3.add_bar().add_volta(0).add_volta(1);
    vs = s3.voltas();
    ASSERT_EQ(2, vs.size());
    ASSERT_EQ(2, vs[0].size());
    EXPECT_EQ(0, vs[0][0]);
    EXPECT_EQ(1, vs[0][1]);
    ASSERT_EQ(1, vs[1].size());
    EXPECT_EQ(1, vs[1][0]);
}
