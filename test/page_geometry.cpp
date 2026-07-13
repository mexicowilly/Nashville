#include <gtest/gtest.h>
#include "../page_geometry.hpp"

using namespace nashville;

TEST(page_geometry, letter_size_in_points)
{
    QSizeF sz = page_size_points(page_size::letter);
    EXPECT_DOUBLE_EQ(sz.width(),  612.0);
    EXPECT_DOUBLE_EQ(sz.height(), 792.0);
}

TEST(page_geometry, a4_size_in_points)
{
    QSizeF sz = page_size_points(page_size::a4);
    EXPECT_NEAR(sz.width(),  595.2756, 1e-3);
    EXPECT_NEAR(sz.height(), 841.8898, 1e-3);
}

TEST(page_geometry, unit_conversion_round_trips)
{
    EXPECT_DOUBLE_EQ(to_points(1.0, length_unit::inches), 72.0);
    EXPECT_NEAR(from_points(72.0, length_unit::inches), 1.0, 1e-9);

    const qreal one_cm_in_points = to_points(1.0, length_unit::centimeters);
    EXPECT_NEAR(from_points(one_cm_in_points, length_unit::centimeters), 1.0, 1e-9);
    EXPECT_NEAR(from_points(to_points(1.0, length_unit::inches), length_unit::centimeters),
                2.54, 1e-9);
}

TEST(page_geometry, content_area_subtracts_margins)
{
    page_geometry geo;
    geo.size = QSizeF(612.0, 792.0);
    geo.margins = { 72.0, 72.0, 72.0, 72.0 };
    EXPECT_DOUBLE_EQ(geo.content_width(),  612.0 - 144.0);
    EXPECT_DOUBLE_EQ(geo.content_height(), 792.0 - 144.0);
}

TEST(page_geometry, content_area_never_goes_negative)
{
    page_geometry geo;
    geo.size = QSizeF(100.0, 100.0);
    geo.margins = { 80.0, 80.0, 80.0, 80.0 };
    EXPECT_DOUBLE_EQ(geo.content_width(),  0.0);
    EXPECT_DOUBLE_EQ(geo.content_height(), 0.0);
}

TEST(page_geometry, paginate_everything_fits_on_one_page)
{
    std::vector<qreal> heights = { 100.0, 100.0, 100.0 };
    auto result = paginate(500.0, heights);
    EXPECT_EQ(result.page_count, 1);
    ASSERT_EQ(result.positions.size(), 3u);
    EXPECT_EQ(result.positions[0].page_index, 0);
    EXPECT_DOUBLE_EQ(result.positions[0].y_in_page_content, 0.0);
    EXPECT_EQ(result.positions[1].page_index, 0);
    EXPECT_DOUBLE_EQ(result.positions[1].y_in_page_content, 100.0);
    EXPECT_EQ(result.positions[2].page_index, 0);
    EXPECT_DOUBLE_EQ(result.positions[2].y_in_page_content, 200.0);
}

TEST(page_geometry, paginate_breaks_between_items_never_mid_item)
{
    std::vector<qreal> heights = { 100.0, 100.0, 100.0 };
    auto result = paginate(250.0, heights);
    EXPECT_EQ(result.page_count, 2);
    ASSERT_EQ(result.positions.size(), 3u);
    EXPECT_EQ(result.positions[0].page_index, 0);
    EXPECT_EQ(result.positions[1].page_index, 0);
    EXPECT_EQ(result.positions[2].page_index, 1);
    EXPECT_DOUBLE_EQ(result.positions[2].y_in_page_content, 0.0);
}

TEST(page_geometry, paginate_oversized_item_gets_its_own_page_without_looping)
{
    std::vector<qreal> heights = { 50.0, 1000.0, 50.0 };
    auto result = paginate(100.0, heights);
    EXPECT_EQ(result.page_count, 3);
    ASSERT_EQ(result.positions.size(), 3u);
    EXPECT_EQ(result.positions[0].page_index, 0);
    EXPECT_EQ(result.positions[1].page_index, 1);
    EXPECT_DOUBLE_EQ(result.positions[1].y_in_page_content, 0.0);
    EXPECT_EQ(result.positions[2].page_index, 2);
}

TEST(page_geometry, paginate_empty_input)
{
    auto result = paginate(500.0, {});
    EXPECT_EQ(result.page_count, 1);
    EXPECT_TRUE(result.positions.empty());
}

TEST(page_geometry, paginate_non_positive_content_height_is_one_page)
{
    std::vector<qreal> heights = { 10.0, 20.0, 30.0 };
    auto result = paginate(0.0, heights);
    EXPECT_EQ(result.page_count, 1);
    for (const auto& p : result.positions)
        EXPECT_EQ(p.page_index, 0);
}

TEST(page_geometry, absolute_y_page_zero_uses_content_top_override)
{
    page_geometry geo;
    geo.size = QSizeF(612.0, 792.0);
    geo.margins = { 72.0, 72.0, 72.0, 72.0 };
    flow_position pos{ 0, 40.0 };
    EXPECT_DOUBLE_EQ(absolute_y(geo, 150.0, pos), 190.0);
}

TEST(page_geometry, absolute_y_later_pages_start_at_margin_top)
{
    page_geometry geo;
    geo.size = QSizeF(612.0, 792.0);
    geo.margins = { 72.0, 72.0, 72.0, 72.0 };
    flow_position pos{ 1, 10.0 };
    const qreal expected = 1.0 * (792.0 + k_inter_page_gap) + 72.0 + 10.0;
    EXPECT_DOUBLE_EQ(absolute_y(geo, 150.0, pos), expected);
}

TEST(page_geometry, total_canvas_height_accounts_for_gaps)
{
    page_geometry geo;
    geo.size = QSizeF(612.0, 792.0);
    EXPECT_DOUBLE_EQ(total_canvas_height(geo, 1), 792.0);
    EXPECT_DOUBLE_EQ(total_canvas_height(geo, 3),
                      3.0 * 792.0 + 2.0 * k_inter_page_gap);
}

TEST(page_geometry, total_canvas_height_clamps_page_count_to_at_least_one)
{
    page_geometry geo;
    geo.size = QSizeF(612.0, 792.0);
    EXPECT_DOUBLE_EQ(total_canvas_height(geo, 0), 792.0);
    EXPECT_DOUBLE_EQ(total_canvas_height(geo, -5), 792.0);
}
