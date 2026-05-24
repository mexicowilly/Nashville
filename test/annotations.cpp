#include <gtest/gtest.h>
#include "../model/annotations.hpp"

using namespace nashville;

// The annotation_layer test is harder to write standalone: it depends
// on lines_ being populated by song_body_widget's layout pass, plus a
// running Qt event loop for QTest::mouseClick.  See the test file
// pattern in test/database.cpp for the closest analog if you want to
// add full UI tests later.  These tests stay at the model level where
// the contract is simple.

TEST(annotations, add_and_retrieve_text_box)
{
    model::annotations a;
    EXPECT_TRUE(a.empty());

    auto& tb = a.add_text_box(QRectF(10, 20, 100, 40), "hello");
    EXPECT_FALSE(a.empty());
    EXPECT_EQ(1u, a.text_boxes().size());
    EXPECT_EQ("hello", tb.text);
    EXPECT_NE(0u, tb.id);                  // 0 reserved for "no id"
    EXPECT_EQ(QRectF(10, 20, 100, 40), tb.rect);
}

TEST(annotations, add_and_retrieve_connector)
{
    model::annotations a;
    auto& c = a.add_connector(QPointF(1, 2), QPointF(3, 4));
    EXPECT_EQ(1u, a.connectors().size());
    // The (QPointF, QPointF) overload makes both endpoints free.
    EXPECT_EQ(model::connector_endpoint::kind::free, c.start.k);
    EXPECT_EQ(model::connector_endpoint::kind::free, c.end.k);
    EXPECT_EQ(QPointF(1, 2), c.start.free_pos);
    EXPECT_EQ(QPointF(3, 4), c.end.free_pos);
    EXPECT_FALSE(c.arrow_at_start);
    EXPECT_TRUE (c.arrow_at_end);          // default: arrow on end
}

TEST(annotations, add_connector_with_anchored_endpoint)
{
    model::annotations a;
    auto& tb = a.add_text_box(QRectF(0, 0, 100, 50));
    auto& c = a.add_connector(
        model::connector_endpoint::make_anchor(tb.id, 3),  // E anchor
        model::connector_endpoint::make_free(QPointF(200, 100)));
    EXPECT_EQ(model::connector_endpoint::kind::text_box_anchor, c.start.k);
    EXPECT_EQ(tb.id, c.start.text_box_id);
    EXPECT_EQ(3u,    c.start.anchor_index);
    EXPECT_EQ(model::connector_endpoint::kind::free, c.end.k);
    EXPECT_EQ(QPointF(200, 100), c.end.free_pos);
}

TEST(annotations, remove_text_box_unsticks_connectors)
{
    // The whole point of the new endpoint model: a connector glued to
    // a text box should survive that text box being deleted, with the
    // glued endpoint converted to free at the anchor's last position.
    model::annotations a;
    a.set_anchor_resolver(
        [&](std::uint64_t tb_id, unsigned idx) -> QPointF {
            const auto* tb = a.find_text_box(tb_id);
            if (!tb) return QPointF(0, 0);
            // E anchor of a 100x50 box at (10, 20): right edge midline.
            // For this test we only care about idx == 3 (E).
            if (idx == 3) return QPointF(tb->rect.right(), tb->rect.center().y());
            return QPointF(0, 0);
        });
    auto& tb = a.add_text_box(QRectF(10, 20, 100, 50));
    const auto tb_id = tb.id;
    auto& c = a.add_connector(
        model::connector_endpoint::make_anchor(tb_id, 3),
        model::connector_endpoint::make_free(QPointF(500, 500)));
    const auto c_id = c.id;

    EXPECT_TRUE(a.remove_text_box(tb_id));
    EXPECT_EQ(0u, a.text_boxes().size());
    EXPECT_EQ(1u, a.connectors().size());

    const auto* c_after = a.find_connector(c_id);
    ASSERT_NE(nullptr, c_after);
    // Glued end should now be free at the resolved anchor position
    // (E of the 100x50 box at (10, 20) → x=110, y=45).
    EXPECT_EQ(model::connector_endpoint::kind::free, c_after->start.k);
    EXPECT_EQ(QPointF(110, 45), c_after->start.free_pos);
    // The other end was already free; it's unchanged.
    EXPECT_EQ(model::connector_endpoint::kind::free, c_after->end.k);
    EXPECT_EQ(QPointF(500, 500), c_after->end.free_pos);
}

TEST(annotations, remove_text_box_with_no_resolver_uses_origin)
{
    // Defensive: without a resolver installed (test-only path —
    // production wires one from the view), glued endpoints become
    // free at (0, 0).  This is documented behavior; verify it.
    model::annotations a;
    auto& tb = a.add_text_box(QRectF(10, 20, 100, 50));
    const auto tb_id = tb.id;
    auto& c = a.add_connector(
        model::connector_endpoint::make_anchor(tb_id, 3),
        model::connector_endpoint::make_free(QPointF(500, 500)));
    const auto c_id = c.id;
    EXPECT_TRUE(a.remove_text_box(tb_id));
    const auto* c_after = a.find_connector(c_id);
    ASSERT_NE(nullptr, c_after);
    EXPECT_EQ(QPointF(0, 0), c_after->start.free_pos);
}

TEST(annotations, remove_text_box_leaves_unrelated_connectors_alone)
{
    // A connector that wasn't glued to the removed text box must come
    // out of the operation unchanged.
    model::annotations a;
    const auto tb1_id = a.add_text_box(QRectF(0, 0, 10, 10)).id;
    const auto tb2_id = a.add_text_box(QRectF(20, 20, 10, 10)).id;
    // Glue to tb2, then remove tb1.  Capture the id immediately
    // because add_connector may reallocate the connector vector, but
    // more importantly we already captured the text-box ids above
    // before that vector grew (the earlier version of this test held
    // a reference into text_boxes_ across the second add_text_box,
    // which std::vector explicitly invalidates).
    const auto c_id = a.add_connector(
        model::connector_endpoint::make_anchor(tb2_id, 0),
        model::connector_endpoint::make_free(QPointF(99, 99))).id;
    EXPECT_TRUE(a.remove_text_box(tb1_id));
    const auto* c_after = a.find_connector(c_id);
    ASSERT_NE(nullptr, c_after);
    EXPECT_EQ(model::connector_endpoint::kind::text_box_anchor, c_after->start.k);
    EXPECT_EQ(tb2_id, c_after->start.text_box_id);
}

TEST(annotations, ids_are_unique_and_monotonic)
{
    model::annotations a;
    auto t1 = a.add_text_box (QRectF(0, 0, 1, 1)).id;
    auto c1 = a.add_connector(QPointF(0, 0), QPointF(1, 1)).id;
    auto t2 = a.add_text_box (QRectF(0, 0, 1, 1)).id;
    // The two id pools are unified — they all come from next_id_ in
    // the model, so collisions can't happen across types.
    EXPECT_NE(t1, c1);
    EXPECT_NE(c1, t2);
    EXPECT_NE(t1, t2);
    EXPECT_LT(t1, c1);
    EXPECT_LT(c1, t2);
}

TEST(annotations, find_returns_null_for_missing_id)
{
    model::annotations a;
    EXPECT_EQ(nullptr, a.find_text_box(999));
    EXPECT_EQ(nullptr, a.find_connector(999));
    auto& tb = a.add_text_box(QRectF(0, 0, 1, 1), "x");
    EXPECT_NE(nullptr, a.find_text_box(tb.id));
    EXPECT_EQ(nullptr, a.find_connector(tb.id));  // type-specific lookup
}

TEST(annotations, remove_text_box)
{
    model::annotations a;
    auto id = a.add_text_box(QRectF(0, 0, 1, 1)).id;
    EXPECT_TRUE (a.remove_text_box(id));
    EXPECT_FALSE(a.remove_text_box(id));  // second remove is a no-op
    EXPECT_TRUE (a.text_boxes().empty());
}

TEST(annotations, remove_connector)
{
    model::annotations a;
    auto id = a.add_connector(QPointF(0, 0), QPointF(1, 1)).id;
    EXPECT_TRUE (a.remove_connector(id));
    EXPECT_FALSE(a.remove_connector(id));
    EXPECT_TRUE (a.connectors().empty());
}

TEST(annotations, find_returns_mutable_pointer)
{
    model::annotations a;
    auto id = a.add_text_box(QRectF(0, 0, 1, 1), "before").id;
    auto* tb = a.find_text_box(id);
    ASSERT_NE(nullptr, tb);
    tb->text = "after";
    // Verify the mutation persisted via a fresh lookup.
    EXPECT_EQ("after", a.find_text_box(id)->text);
}

TEST(annotations, load_reseeds_next_id)
{
    // The database load path hands annotations::load() a pair of
    // pre-id'd vectors and expects subsequent add_*() calls to assign
    // ids one past the maximum loaded id.  This is what prevents id
    // collisions between persisted annotations and ones the user adds
    // in the same session after loading.
    model::annotations a;
    std::vector<model::text_box>  tbs;
    std::vector<model::connector> cs;

    model::text_box t1; t1.id = 5;  t1.rect = QRectF(0, 0, 1, 1); tbs.push_back(t1);
    model::text_box t2; t2.id = 17; t2.rect = QRectF(0, 0, 1, 1); tbs.push_back(t2);
    model::connector c1; c1.id = 3;
    c1.start = model::connector_endpoint::make_free(QPointF(0, 0));
    c1.end   = model::connector_endpoint::make_free(QPointF(1, 1));
    cs.push_back(c1);

    a.load(std::move(tbs), std::move(cs));
    EXPECT_EQ(2u, a.text_boxes().size());
    EXPECT_EQ(1u, a.connectors().size());

    // Highest loaded id was 17 (across both types), so the next add
    // should hand out id 18.
    auto& fresh = a.add_text_box(QRectF(0, 0, 1, 1));
    EXPECT_EQ(18u, fresh.id);

    auto& fresh_c = a.add_connector(QPointF(0, 0), QPointF(1, 1));
    EXPECT_EQ(19u, fresh_c.id);
}

TEST(annotations, load_replaces_existing_contents)
{
    model::annotations a;
    a.add_text_box(QRectF(0, 0, 1, 1), "discarded");
    a.add_connector(QPointF(0, 0), QPointF(1, 1));

    // Load with empty vectors should leave the container empty.
    a.load({}, {});
    EXPECT_TRUE(a.empty());

    // And next_id_ should reseed to 1 (max id = 0 + 1).
    auto& tb = a.add_text_box(QRectF(0, 0, 1, 1));
    EXPECT_EQ(1u, tb.id);
}
