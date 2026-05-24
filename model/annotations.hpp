#pragma once

#include "../loggable.hpp"
#include <QPointF>
#include <QRectF>
#include <QString>
#include <vector>
#include <functional>
#include <cstdint>

namespace nashville::model
{

// A free-floating text box overlaid on the song chart.  Coordinates are
// "song-local": origin at the top-left of the chart content area (i.e.
// just inside the margin/divider on the left and the title bar above),
// so a horizontal divider drag does not move annotations relative to
// the chart.  See annotation_layer for the conversion to widget coords.
struct text_box
{
    std::uint64_t id = 0;     // stable across edits; assigned by container
    QRectF rect;              // song-local coords
    QString text;
};

// One end of a connector.  Two flavors:
//
//   * free:            the endpoint is at a fixed song-local coordinate.
//                      Doesn't follow anything.  This was the only kind
//                      we used to support, and it's still the default
//                      for endpoints dropped on empty canvas, on a
//                      line-of-bars edge anchor, or anywhere that
//                      doesn't snap to a text box.
//
//   * text_box_anchor: the endpoint is glued to a specific anchor point
//                      (one of the 8 around the box's border) of a
//                      specific text box.  When the text box moves or
//                      resizes, the endpoint follows.  This is the
//                      Google-Drawings "connector stays attached" rule,
//                      but scoped specifically to text-box anchors —
//                      line-of-bars anchors don't glue because the
//                      bars themselves reflow.
//
// The struct uses a discriminated union pattern (tag + storage) rather
// than std::variant: it keeps SQLite binding straightforward (the kind
// is a single int, the free coords are nullable doubles, the anchor
// fields are nullable id/index), and avoids forcing every translation
// unit that includes this header to pull in <variant>.
struct connector_endpoint
{
    enum class kind { free, text_box_anchor };
    kind k = kind::free;

    // Valid iff k == free.
    QPointF free_pos;

    // Valid iff k == text_box_anchor.
    std::uint64_t text_box_id = 0;
    unsigned      anchor_index = 0;   // 0..7, indexing into
                                      // annotation_layer::text_box_anchors()

    // Factory helpers — keep call sites readable.  No constructors
    // because aggregate-init is convenient elsewhere; these are static
    // makers for the cases where the caller really means "give me a
    // free endpoint at (x, y)" or "glue to (id, anchor)".
    static connector_endpoint make_free(const QPointF& p)
    {
        connector_endpoint e;
        e.k = kind::free;
        e.free_pos = p;
        return e;
    }
    static connector_endpoint make_anchor(std::uint64_t id, unsigned idx)
    {
        connector_endpoint e;
        e.k = kind::text_box_anchor;
        e.text_box_id  = id;
        e.anchor_index = idx;
        return e;
    }
};

// A straight line with optional arrowheads.  Each endpoint is either
// free (raw coordinate) or glued to a specific text-box anchor.  The
// model doesn't know how to resolve an anchored endpoint to a pixel
// position — that's the view's job (annotation_layer::resolve_endpoint)
// because the view owns the text_box_anchors() geometry helper.
struct connector
{
    std::uint64_t id = 0;
    connector_endpoint start;
    connector_endpoint end;
    bool arrow_at_start = false;
    bool arrow_at_end   = true;
};

class annotations : public loggable
{
public:
    annotations();

    // Read access.  Pointer stability is NOT guaranteed across
    // mutations (we store in std::vector and may relocate on grow), so
    // callers that need to refer to a specific annotation across
    // operations should hold the id, not a pointer.
    const std::vector<text_box>&  text_boxes()  const { return text_boxes_; }
    const std::vector<connector>& connectors() const { return connectors_; }

    // Returns a reference to the newly-appended item.  Reference
    // validity ends at the next mutation, same caveat as above.
    text_box& add_text_box(const QRectF& rect, const QString& text = {});

    // Two overloads of add_connector for ergonomics:
    //   * The (QPointF, QPointF) form creates a connector with two free
    //     endpoints.  Used by the rubber-band commit path when the user
    //     dropped both ends on empty canvas.
    //   * The (endpoint, endpoint) form is the general case — either
    //     end can be free or anchored to a text box.
    connector& add_connector(const QPointF& a, const QPointF& b);
    connector& add_connector(const connector_endpoint& a,
                             const connector_endpoint& b);

    // Lookup by id.  nullptr if not found.
    text_box*       find_text_box(std::uint64_t id);
    const text_box* find_text_box(std::uint64_t id) const;
    connector*       find_connector(std::uint64_t id);
    const connector* find_connector(std::uint64_t id) const;

    // Returns true iff something was removed.  Removing a text box
    // doesn't cascade-delete connectors that were glued to it; instead
    // it walks the connector list and converts any glued endpoints
    // back to free, freezing them at the last-known anchor position.
    // The view supplies that position via the resolver callback —
    // model can't compute it itself without the text_box_anchors()
    // helper, which lives in the view layer.  If no resolver is given
    // we fall back to converting to free at (0, 0); that's good enough
    // for tests but the view always installs a resolver.
    bool remove_text_box(std::uint64_t id);
    bool remove_connector(std::uint64_t id);

    // Resolver type and setter.  The view installs a function that,
    // given (text_box_id, anchor_index), returns the song-local
    // coordinate of that anchor right now.  Used during text-box
    // deletion to freeze glued endpoints in place.  Optional — if not
    // set, anchored endpoints become free at (0, 0) on deletion.
    using anchor_resolver =
        std::function<QPointF(std::uint64_t, unsigned)>;
    void set_anchor_resolver(anchor_resolver r) { resolve_anchor_ = std::move(r); }

    // Wholesale replacement — used by the database load path.  IDs in
    // the incoming vectors are trusted; next_id_ is bumped to one past
    // the maximum so subsequent add_*() calls don't collide.
    void load(std::vector<text_box> tbs, std::vector<connector> cs);

    bool empty() const
    {
        return text_boxes_.empty() && connectors_.empty();
    }

private:
    std::vector<text_box>  text_boxes_;
    std::vector<connector> connectors_;
    anchor_resolver        resolve_anchor_;

    // Monotonic id source.  Starts at 1 so 0 is reserved as a "no id"
    // sentinel — the drag state machine uses 0 to mean "not targeting
    // any existing annotation."
    std::uint64_t next_id_ = 1;
};

} // namespace nashville::model
