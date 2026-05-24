#include "annotation_layer.hpp"
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QColor>
#include <QPolygonF>
#include <QFontMetricsF>
#include <QTextOption>
#include <cmath>
#include <algorithm>
#include <limits>

namespace nashville::view
{

namespace
{

// Visual palette.  Colors mirror Google Drawings:
//   * Anchor dots use the same desaturated purple Google uses for
//     connection-point hints.  It reads as "interactive but not
//     selected" — distinct from the bar-selection light gray, distinct
//     from the dark gray of chart rules, and not confusable with the
//     blue square handles that mean "selected and grabbable."
//   * Selection chrome (handles around a selected text box, endpoint
//     handles on a selected connector) uses a saturated blue, again
//     matching Google.  The hue difference between anchor-purple and
//     selection-blue is the user's signal: "purple = where I could
//     attach", "blue = what I've selected."
const QColor k_anchor_color      (155,  80, 200);          // Google connection-point purple
const QColor k_anchor_hover_color(120,  50, 175);          // deeper purple on hover/snap
const QColor k_anchor_outline    (255, 255, 255, 220);    // near-opaque white ring around every dot
const QColor k_connector_color   (60, 60, 60);
const QColor k_selection_color   (26, 115, 232);           // Google selection blue
const QColor k_text_box_border   (150, 150, 150);
const QColor k_text_box_text     (40, 40, 40);
const QColor k_text_box_placeholder(170, 170, 170);

// Squared distance — saves a sqrt on hot snap-distance paths.
inline qreal d2(const QPointF& a, const QPointF& b)
{
    const qreal dx = a.x() - b.x();
    const qreal dy = a.y() - b.y();
    return dx * dx + dy * dy;
}

} // namespace

// ===========================================================================
// Construction
// ===========================================================================
annotation_layer::annotation_layer(model::annotations& anns,
                                   const std::vector<line_layout>& lines,
                                   std::function<QRectF()> chart_content_rect)
    : annotations_(anns)
    , lines_(lines)
    , chart_content_rect_(std::move(chart_content_rect))
{
}

// ===========================================================================
// Tool mode
// ===========================================================================
void annotation_layer::set_tool(tool t)
{
    if (tool_ == t)
        return;
    // Switching tools mid-drag would leave us in a half-defined state.
    // Cancel any in-flight drag first, then drop selection so the new
    // tool starts from a clean slate.  Also clear hover_pos_ so a
    // lingering "near a shape" position from the old tool doesn't
    // paint anchors in the new tool's first frame.
    drag_ = drag_kind::none;
    drag_target_id_      = 0;
    drag_orig_text_box_.reset();
    drag_orig_connector_.reset();
    hover_pos_.reset();
    clear_selection();
    tool_ = t;
}

// ===========================================================================
// Coordinate conversion
// ===========================================================================
QPointF annotation_layer::to_widget(const QPointF& song_pt) const
{
    const QRectF cr = chart_content_rect_();
    return QPointF(song_pt.x() + cr.left(), song_pt.y() + cr.top());
}

QRectF annotation_layer::to_widget(const QRectF& song_rect) const
{
    const QRectF cr = chart_content_rect_();
    return QRectF(song_rect.x() + cr.left(),
                  song_rect.y() + cr.top(),
                  song_rect.width(),
                  song_rect.height());
}

QPointF annotation_layer::to_song(const QPointF& widget_pt) const
{
    const QRectF cr = chart_content_rect_();
    return QPointF(widget_pt.x() - cr.left(), widget_pt.y() - cr.top());
}

// ===========================================================================
// Anchor enumeration
// ===========================================================================
// Ordering: NW, N, NE, E, SE, S, SW, W.  Documented in the header.
// This ordering makes "rotate handles by 45°" trivial if we ever want
// it, and gives a stable index for resize-handle behavior (opposite
// handle is always (i + 4) % 8).
std::array<QPointF, 8>
annotation_layer::text_box_anchors(const QRectF& r)
{
    const qreal l = r.left();
    const qreal t = r.top();
    const qreal rt = r.right();
    const qreal b  = r.bottom();
    const qreal cx = r.center().x();
    const qreal cy = r.center().y();
    return {
        QPointF(l,  t),     // 0 NW
        QPointF(cx, t),     // 1 N
        QPointF(rt, t),     // 2 NE
        QPointF(rt, cy),    // 3 E
        QPointF(rt, b),     // 4 SE
        QPointF(cx, b),     // 5 S
        QPointF(l,  b),     // 6 SW
        QPointF(l,  cy),    // 7 W
    };
}

// (line_edge_anchors used to live here; removed when line-of-bars
// edges were dropped as anchors — see snap_at's comment.  The
// declaration in the header is gone too.)

// Pulls together every anchor the user should see during an endpoint
// drag.  Called only when drag_ is new_connector or move_endpoint —
// anchors are otherwise invisible.
std::vector<QPointF> annotation_layer::all_visible_anchors() const
{
    // Only text-box anchors appear during a drag.  Line-of-bars edges
    // used to advertise as anchors here too, but they don't glue and
    // we no longer show them — see the matching comment in snap_at.
    std::vector<QPointF> out;
    for (const auto& tb : annotations_.text_boxes())
    {
        const QRectF wr = to_widget(tb.rect);
        const auto pts = text_box_anchors(wr);
        out.insert(out.end(), pts.begin(), pts.end());
    }
    return out;
}

// Rich snap query.  Walks text-box anchors first (so a snap-tie
// between a text-box anchor and an overlapping line-edge anchor goes
// to the text box, which is what users expect — text-box anchors are
// the gluing target), then line-of-bars anchors.  For text-box hits
// we record the box id and which anchor index it was (0..7); for
// line-edge hits we just record the position.  The caller decides
// what to do with the discriminator: rubber-band visuals don't care
// (they just use .pos), but commit-on-release does.
std::optional<annotation_layer::snap_result>
annotation_layer::snap_at(const QPointF& widget_pt) const
{
    // Only text-box anchors are snap targets.  Line-of-bars edges
    // used to advertise as anchors here too, but they don't glue
    // (bar rows reflow on resize / bars_per_line change) and visually
    // suggesting "this is an attachment point" when it actually isn't
    // is misleading — the user expects the connector to follow.
    // Removing them entirely is the simpler answer; the snap radius
    // is small enough that a user who actually wants to land on a
    // bar's edge can just drop the endpoint there without snapping.
    const qreal r2 = k_snap_radius * k_snap_radius;
    std::optional<snap_result> best;
    qreal best_d2 = std::numeric_limits<qreal>::max();

    // Walk back-to-front so a snap-tie among overlapping boxes goes
    // to the top one, matching the paint Z order and the hit-test
    // priority.
    const auto& tbs = annotations_.text_boxes();
    for (auto it = tbs.rbegin(); it != tbs.rend(); ++it)
    {
        const auto pts = text_box_anchors(to_widget(it->rect));
        for (unsigned i = 0; i < pts.size(); ++i)
        {
            const qreal cur = d2(pts[i], widget_pt);
            if (cur <= r2 && cur < best_d2)
            {
                best_d2 = cur;
                snap_result sr;
                sr.pos                = pts[i];
                sr.is_text_box_anchor = true;
                sr.text_box_id        = it->id;
                sr.anchor_index       = i;
                best                  = sr;
            }
        }
    }
    return best;
}

std::optional<QPointF>
annotation_layer::snap_to_anchor(const QPointF& widget_pt) const
{
    // Thin wrapper for legacy call sites that only need the position.
    if (auto s = snap_at(widget_pt)) return s->pos;
    return std::nullopt;
}

// Resolve an endpoint to its current widget-coordinate position.
// Defensive against a stale text_box_id (which should never happen
// because annotations::remove_text_box converts glued endpoints to
// free before erasing the box) — if the referenced box is gone we
// return (0, 0) and log.
QPointF annotation_layer::resolve_endpoint(const model::connector_endpoint& ep) const
{
    if (ep.k == model::connector_endpoint::kind::free)
        return to_widget(ep.free_pos);

    // text_box_anchor
    if (const auto* tb = annotations_.find_text_box(ep.text_box_id))
    {
        const auto pts = text_box_anchors(to_widget(tb->rect));
        if (ep.anchor_index < pts.size())
            return pts[ep.anchor_index];
    }
    // Lost target — shouldn't be reachable.  Return (0, 0) so we
    // don't crash; the connector will be visibly broken, which is
    // the loudest signal we can give without a model-side guarantee
    // we don't have here.
    return QPointF(0, 0);
}

// Inverse: build an endpoint from a cursor position, gluing if the
// position happens to lie on a text-box anchor.  Used by the drag-
// commit paths in mouse_release.  Note that we feed the *un-snapped*
// cursor position in — the resulting endpoint either snaps (and
// becomes anchored) or doesn't (and becomes free at the cursor).
model::connector_endpoint
annotation_layer::make_endpoint(const QPointF& widget_pos) const
{
    if (auto s = snap_at(widget_pos))
    {
        if (s->is_text_box_anchor)
            return model::connector_endpoint::make_anchor(
                s->text_box_id, s->anchor_index);
        // Line-edge anchor: snap visually but stay free.  Endpoint
        // takes the snapped position so it lines up with the bar
        // edge for the moment, but won't follow if bars reflow.
        return model::connector_endpoint::make_free(to_song(s->pos));
    }
    return model::connector_endpoint::make_free(to_song(widget_pos));
}

// ===========================================================================
// Hit-testing
// ===========================================================================
// Priority order is documented in the header: selected-only handles
// first (they're tiny and could be missed if we let the body grab the
// click), then bodies, with connectors above text boxes to match the
// paint Z-order.
annotation_layer::hit
annotation_layer::hit_test(const QPointF& p) const
{
    const qreal endpoint_r2 = k_endpoint_hit_r * k_endpoint_hit_r;
    const qreal handle_r2   = k_text_box_hit_r * k_text_box_hit_r;

    // 1. Selected connector's endpoint handles.
    if (selected_connector_)
    {
        if (const auto* c = annotations_.find_connector(*selected_connector_))
        {
            const QPointF s = resolve_endpoint(c->start);
            const QPointF e = resolve_endpoint(c->end);
            if (d2(p, s) <= endpoint_r2) return { hit::kind::connector_endpoint, c->id, 0 };
            if (d2(p, e) <= endpoint_r2) return { hit::kind::connector_endpoint, c->id, 1 };
        }
    }
    // 2. Selected text box's resize handles.
    if (selected_text_box_)
    {
        if (const auto* tb = annotations_.find_text_box(*selected_text_box_))
        {
            const auto anchors = text_box_anchors(to_widget(tb->rect));
            for (unsigned i = 0; i < anchors.size(); ++i)
            {
                if (d2(p, anchors[i]) <= handle_r2)
                    return { hit::kind::text_box_handle, tb->id, i };
            }
        }
    }
    // 3. Connector bodies (above text boxes, like the paint order).
    //    Iterate back-to-front so the most-recently-drawn — visually on
    //    top — wins a click that overlaps multiple connectors.
    const auto& conns = annotations_.connectors();
    for (auto it = conns.rbegin(); it != conns.rend(); ++it)
    {
        const QPointF s = resolve_endpoint(it->start);
        const QPointF e = resolve_endpoint(it->end);
        if (point_to_segment_distance(p, s, e) <= k_body_hit_tolerance)
            return { hit::kind::connector_body, it->id, 0 };
    }
    // 4. Text-box bodies.  Same back-to-front rule.
    const auto& tbs = annotations_.text_boxes();
    for (auto it = tbs.rbegin(); it != tbs.rend(); ++it)
    {
        if (to_widget(it->rect).contains(p))
            return { hit::kind::text_box_body, it->id, 0 };
    }
    return { hit::kind::none, 0, 0 };
}

qreal annotation_layer::point_to_segment_distance(const QPointF& p,
                                                  const QPointF& a,
                                                  const QPointF& b)
{
    // Standard perpendicular-distance-to-segment.  Falls back to
    // distance-to-endpoint when the projection lies outside [a, b].
    const qreal vx = b.x() - a.x();
    const qreal vy = b.y() - a.y();
    const qreal len2 = vx * vx + vy * vy;
    if (len2 <= 1e-9)
        return std::sqrt(d2(p, a));   // degenerate: a == b
    const qreal t = ((p.x() - a.x()) * vx + (p.y() - a.y()) * vy) / len2;
    const qreal tc = std::clamp(t, qreal{0}, qreal{1});
    const QPointF proj(a.x() + tc * vx, a.y() + tc * vy);
    return std::sqrt(d2(p, proj));
}

// ===========================================================================
// Mouse events
// ===========================================================================
// The dispatch reads top-down:
//   1. Endpoint / handle drag on an already-selected annotation.
//   2. Click on an existing annotation body → select (or, if already
//      selected, start a body translate).
//   3. Click on empty canvas with a tool active → start creation.
// In tool::none mode we still claim presses on annotation bodies (so
// the user can delete things they've drawn without switching tools),
// but presses on empty canvas fall through (return false) for the host
// to handle as normal chart interaction.
bool annotation_layer::mouse_press(const QPointF& p,
                                   Qt::MouseButton button,
                                   Qt::KeyboardModifiers /*mods*/)
{
    if (button != Qt::LeftButton)
        return false;

    // If a drag is somehow already in flight (shouldn't happen — release
    // always closes it), drop it.  Defensive.
    if (drag_ != drag_kind::none)
    {
        drag_ = drag_kind::none;
        drag_orig_text_box_.reset();
        drag_orig_connector_.reset();
    }

    const hit h = hit_test(p);

    // --- Handle / endpoint drag on selected annotation ------------------
    if (h.k == hit::kind::connector_endpoint)
    {
        // Snapshot current geometry for Esc-cancel.
        if (const auto* c = annotations_.find_connector(h.id))
            drag_orig_connector_ = *c;
        drag_              = drag_kind::move_endpoint;
        drag_target_id_    = h.id;
        drag_handle_idx_   = h.handle;
        // drag_anchor_ = the *other* endpoint (the fixed end), so the
        // rubber-band visual works uniformly with new_connector.
        // resolve_endpoint handles both free and glued kinds.
        if (const auto* c = annotations_.find_connector(h.id))
            drag_anchor_ = resolve_endpoint(h.handle == 0 ? c->end : c->start);
        drag_current_ = p;
        return true;
    }
    if (h.k == hit::kind::text_box_handle)
    {
        if (const auto* tb = annotations_.find_text_box(h.id))
            drag_orig_text_box_ = *tb;
        drag_            = drag_kind::resize_text_box;
        drag_target_id_  = h.id;
        drag_handle_idx_ = h.handle;
        // Pin the opposite corner / midpoint as the drag anchor.
        if (const auto* tb = annotations_.find_text_box(h.id))
        {
            const auto pts = text_box_anchors(to_widget(tb->rect));
            drag_anchor_ = pts[(h.handle + 4) % 8];
        }
        drag_current_ = p;
        return true;
    }

    // --- Body click: select, and if already selected, start translate --
    if (h.k == hit::kind::connector_body)
    {
        const bool was_selected = selected_connector_ &&
                                  *selected_connector_ == h.id;
        clear_selection();
        selected_connector_ = h.id;
        if (was_selected)
        {
            // Already-selected → drag the body.  Endpoints translate
            // rigidly; no snapping during a body drag (see design doc).
            if (const auto* c = annotations_.find_connector(h.id))
                drag_orig_connector_ = *c;
            drag_           = drag_kind::translate_connector;
            drag_target_id_ = h.id;
            drag_anchor_    = p;     // mouse-down position (delta source)
            drag_current_   = p;
        }
        return true;
    }
    if (h.k == hit::kind::text_box_body)
    {
        const bool was_selected = selected_text_box_ &&
                                  *selected_text_box_ == h.id;
        clear_selection();
        selected_text_box_ = h.id;
        if (was_selected)
        {
            if (const auto* tb = annotations_.find_text_box(h.id))
                drag_orig_text_box_ = *tb;
            drag_           = drag_kind::move_text_box;
            drag_target_id_ = h.id;
            drag_anchor_    = p;
            drag_current_   = p;
        }
        return true;
    }

    // --- Empty canvas hit -----------------------------------------------
    // Selection should always clear when the user clicks empty canvas,
    // regardless of tool.  That matches every other vector editor and
    // means a click-then-drag-out-of-the-target isn't ambiguous.
    const bool had_selection = has_selection();
    clear_selection();

    if (is_connector_tool())
    {
        // Start a new connector drag.  Capture the press position's
        // snap state once: this freezes the start-side endpoint kind
        // for the whole drag.  The end side keeps re-snapping on every
        // move (that's the whole point of the rubber-band feedback),
        // but the start side was decided the instant the user clicked.
        // If they pressed within snap radius of a text-box anchor, the
        // start is glued; if they pressed on empty canvas (or near a
        // line-of-bars edge), it's free.  Whether the resulting
        // connector gets an arrowhead is decided in mouse_release
        // based on which tool was active at the press — we don't bake
        // it into the drag state because it's a single bit and tools
        // don't change mid-drag.
        drag_start_endpoint_ = make_endpoint(p);
        // For the rubber-band visual we still need a widget-coord
        // anchor; resolve our just-built endpoint back to widget space.
        // (For free endpoints this is to_widget(p); for anchored, it's
        // the exact anchor pixel.)
        drag_anchor_  = resolve_endpoint(drag_start_endpoint_);
        drag_         = drag_kind::new_connector;
        drag_current_ = p;
        return true;
    }
    if (tool_ == tool::text_box)
    {
        drag_         = drag_kind::new_text_box;
        drag_anchor_  = p;
        drag_current_ = p;
        return true;
    }

    // tool::none, empty canvas: we ate a selection clear if there was
    // one to clear; otherwise we didn't consume the event so the host
    // can run its own bar/title/margin logic.
    return had_selection;
}

bool annotation_layer::mouse_move(const QPointF& p)
{
    // The move can do up to three things:
    //   1. Advance an in-flight drag's current point — consumes the
    //      event (the bar-side hover logic doesn't need to run during
    //      a drag).
    //   2. Update hover_pos_ for border-on-hover text-box display.
    //      This applies in every tool mode, including bar mode.
    //   3. In connector tool mode, also drive source-side anchor
    //      reveal — the overlay reads hover_pos_ to find which shape
    //      to highlight.  Consumes so the host repaints.
    //
    // Return semantics: true means "consume the event AND repaint."
    // We additionally trigger a repaint when the cursor crosses a
    // text-box boundary in any tool mode (so the 1px border appears /
    // disappears), but we DON'T consume those events — the host still
    // needs to run its own bar-side hover logic afterward.  That
    // "request repaint without consuming" path is handled by calling
    // update() on a stashed flag the host reads via wants_repaint().
    if (drag_ != drag_kind::none)
    {
        drag_current_ = p;
        hover_pos_ = p;
        return true;
    }

    // Always-on hover tracking: detect whether the under-cursor text
    // box changed (entered or exited) so we can request a repaint for
    // the border state change.  We check both the old and the new
    // hover position against every text box; if either-but-not-both
    // matches, a boundary was crossed.
    const std::optional<QPointF> old_hover = hover_pos_;
    hover_pos_ = p;

    auto under_some_text_box = [this](const QPointF& q) {
        for (const auto& tb : annotations_.text_boxes())
            if (to_widget(tb.rect).contains(q))
                return true;
        return false;
    };
    const bool was_over = old_hover && under_some_text_box(*old_hover);
    const bool now_over = under_some_text_box(p);
    hover_repaint_pending_ = (was_over != now_over);

    if (is_connector_tool())
        return true;   // consume + repaint (anchor reveal needs it)

    // Bar mode (or text-box tool): we don't consume the move, but we
    // may still need a repaint when the border state flipped.  The
    // host reads hover_repaint_pending_ via take_hover_repaint() after
    // running its own hover code, and update()s once if true.
    return false;
}

bool annotation_layer::take_hover_repaint()
{
    const bool out = hover_repaint_pending_;
    hover_repaint_pending_ = false;
    return out;
}

bool annotation_layer::mouse_release(const QPointF& p)
{
    if (drag_ == drag_kind::none)
        return false;

    const drag_kind kind = drag_;
    // Reset state before mutating the model — if the model mutation
    // throws or triggers something that asks us to repaint, we want to
    // be in a clean state.
    drag_                = drag_kind::none;
    drag_orig_text_box_.reset();
    drag_orig_connector_.reset();
    const std::uint64_t target = drag_target_id_;
    const unsigned      handle = drag_handle_idx_;
    const QPointF       anchor = drag_anchor_;
    const model::connector_endpoint start_ep = drag_start_endpoint_;
    drag_target_id_  = 0;
    drag_handle_idx_ = 0;
    drag_start_endpoint_ = {};   // reset to default (free at origin)

    // Tracks whether this release was the commit of a one-shot
    // creation gesture.  Both new_connector and new_text_box are
    // triggered by a menu Insert action and should drop back to bar
    // mode automatically once the drag ends — regardless of whether
    // the geometry was committed (normal drop), discarded (stray
    // click below the size threshold), or rejected.  At the end of
    // the switch we set tool_ back to none if this is true.  We
    // can't call set_tool() here because that would also clear
    // selection — and we just selected the newly-created annotation,
    // which we want to keep selected.
    bool was_one_shot = false;

    switch (kind)
    {
    case drag_kind::new_connector:
    {
        was_one_shot = true;
        // Build the end-side endpoint from the release position.
        // make_endpoint will glue to a text-box anchor if the cursor
        // is within snap radius of one; otherwise it produces a free
        // endpoint at the cursor (or at the snapped line-edge
        // position, which also doesn't glue).  The start side was
        // captured at press in start_ep — copying it here means a
        // drag that started on box A and ended on box B produces a
        // connector with both ends glued, which is the natural
        // Google-Drawings behavior.
        const model::connector_endpoint end_ep = make_endpoint(p);

        // Zero-length guard: if both ends resolve to the same pixel
        // (the user clicked without moving), drop the connector
        // silently.  Resolve both via resolve_endpoint to compare in
        // widget coords — comparing endpoint structs directly would
        // miss cases where free coords and an anchor happen to
        // coincide.
        const QPointF s_w = resolve_endpoint(start_ep);
        const QPointF e_w = resolve_endpoint(end_ep);
        if (d2(s_w, e_w) < 4.0)
            break;  // stray click — still drops out of one-shot mode

        auto& c = annotations_.add_connector(start_ep, end_ep);
        // Apply the per-tool arrowhead policy.  add_connector defaults
        // to arrow_at_end=true (the historical default before the
        // tool split), so we have to explicitly clear it for the line
        // tool and leave it set for the arrow tool.  arrow_at_start
        // stays false in both — Google Drawings' "Line Start" picker
        // is a future affordance, not a tool mode.
        c.arrow_at_end = (tool_ == tool::arrow);
        // Select the just-created connector so the user can
        // immediately adjust it.  Matches Google Drawings' behavior
        // of leaving the freshly-drawn line selected.
        selected_connector_ = c.id;
        break;
    }
    case drag_kind::move_endpoint:
    {
        auto* c = annotations_.find_connector(target);
        if (!c)
            break;
        // Build the moved endpoint from the release position, gluing
        // if applicable.  The *other* end stays as it was — we don't
        // touch its kind.  Zero-length guard compares the new moved
        // position against the (resolved) other end in widget coords.
        const model::connector_endpoint moved_ep = make_endpoint(p);
        const QPointF moved_w = resolve_endpoint(moved_ep);
        const QPointF other_w = resolve_endpoint(handle == 0 ? c->end : c->start);
        if (d2(moved_w, other_w) < 4.0)
        {
            // Revert: nothing was written to the model during the
            // drag (rubber-band is overlay-only), so simply not
            // writing here is the revert.
            break;
        }
        (handle == 0 ? c->start : c->end) = moved_ep;
        break;
    }
    case drag_kind::translate_connector:
    {
        auto* c = annotations_.find_connector(target);
        if (!c)
            break;
        // Translating the whole connector preserves each endpoint's
        // kind: free endpoints shift by the delta, anchored endpoints
        // stay glued (their resolved position will naturally move
        // when the underlying text box doesn't, but since we're not
        // moving any text box here, glued endpoints simply don't
        // translate — which is the expected behavior for a body drag,
        // since you can't drag a glued end away from its anchor without
        // grabbing the endpoint specifically).  In practice that means:
        // shift only the free endpoints by the delta.  If both ends
        // are anchored, the body drag is a no-op (and the user can
        // see the connector didn't move — that's their cue to drag
        // an endpoint instead).
        const QPointF delta_widget = p - anchor;
        if (c->start.k == model::connector_endpoint::kind::free)
            c->start.free_pos += delta_widget;
        if (c->end.k == model::connector_endpoint::kind::free)
            c->end.free_pos += delta_widget;
        break;
    }
    case drag_kind::new_text_box:
    {
        was_one_shot = true;
        QRectF r = QRectF(anchor, p).normalized();
        // Tiny rubber-bands are likely accidental clicks.  Threshold
        // here is generous because text boxes need some width to be
        // useful.
        if (r.width() < 20.0 || r.height() < 14.0)
            break;  // stray click — drops out of one-shot mode
        auto& tb = annotations_.add_text_box(QRectF(to_song(r.topLeft()),
                                                    r.size()));
        selected_text_box_ = tb.id;
        // Immediately open the inline editor so the user can type into
        // the box they just drew — same UX as Google Drawings.
        if (edit_text_)
            edit_text_(tb.id, r);
        break;
    }
    case drag_kind::move_text_box:
    {
        auto* tb = annotations_.find_text_box(target);
        if (!tb)
            break;
        const QPointF delta = p - anchor;
        tb->rect.translate(delta);
        break;
    }
    case drag_kind::resize_text_box:
    {
        auto* tb = annotations_.find_text_box(target);
        if (!tb)
            break;
        // The drag_anchor_ for resize is the OPPOSITE corner / midpoint
        // (pinned).  New rect spans from that anchor to the current
        // cursor.  For edge-midpoint handles (N/E/S/W = indices 1, 3,
        // 5, 7) we constrain motion to a single axis so the user can
        // resize purely vertically or horizontally — same affordance
        // every vector editor provides.
        QRectF wr = to_widget(tb->rect);
        QPointF new_pt = p;
        if (handle == 1 || handle == 5)        // N or S: only y changes
            new_pt.setX(anchor.x() == wr.left() ? wr.right() : wr.left());
        else if (handle == 3 || handle == 7)   // E or W: only x changes
            new_pt.setY(anchor.y() == wr.top() ? wr.bottom() : wr.top());
        QRectF new_widget = QRectF(anchor, new_pt).normalized();
        // Refuse to shrink past a minimum size so the box stays
        // grabbable on the next interaction.
        if (new_widget.width() < 20.0)  new_widget.setWidth(20.0);
        if (new_widget.height() < 14.0) new_widget.setHeight(14.0);
        tb->rect = QRectF(to_song(new_widget.topLeft()), new_widget.size());
        break;
    }
    case drag_kind::none:
        break;
    }

    // One-shot reset: if this drag was a menu-triggered creation
    // gesture (new connector or new text box), return to bar mode now
    // that it's complete.  Direct field assignment instead of
    // set_tool(tool::none) because set_tool() also clears selection,
    // and we deliberately kept the freshly-created annotation
    // selected so the user can immediately adjust it.  hover_pos_ is
    // dropped because the connector-tool idle-hover anchors are no
    // longer relevant once we've left the tool.
    if (was_one_shot)
    {
        tool_ = tool::none;
        hover_pos_.reset();
    }
    return true;
}

bool annotation_layer::mouse_double_click(const QPointF& p)
{
    // Double-click on a text box → edit its text.  We do this on press-
    // double rather than waiting for selection-then-something because
    // that matches every text-editing surface in the chart already
    // (title, bars, sections all use the same idiom).
    const hit h = hit_test(p);
    if (h.k == hit::kind::text_box_body)
    {
        if (const auto* tb = annotations_.find_text_box(h.id))
        {
            // Select before editing — visually the wash happens under
            // the editor but it also means a subsequent Esc-cancel
            // leaves a clean selection state.
            clear_selection();
            selected_text_box_ = tb->id;
            if (edit_text_)
                edit_text_(tb->id, to_widget(tb->rect));
            return true;
        }
    }
    return false;
}

void annotation_layer::mouse_leave()
{
    // Drop hover position so the next paint won't show a stale set of
    // anchor dots or text-box borders near the last cursor location.
    // Drag state is preserved — Qt continues to deliver move events
    // while a button is held even when the cursor is outside the
    // widget, so a drag that briefly leaves the widget rim should
    // keep going.  Clear the pending-repaint flag too — leaveEvent
    // already triggers an update() so the flag would be redundantly
    // set.
    hover_pos_.reset();
    hover_repaint_pending_ = false;
}

// ===========================================================================
// Key events
// ===========================================================================
bool annotation_layer::key_press(int key, Qt::KeyboardModifiers /*mods*/)
{
    if (key == Qt::Key_Escape)
    {
        // 1. Mid-drag Esc: cancel the drag and restore geometry.  If
        //    the in-flight drag was a one-shot creation (line / arrow /
        //    text box), also exit the tool — the menu action that
        //    armed it is "do one thing then stop," and a cancelled
        //    one-thing is still done.  For other drag kinds (moving
        //    or resizing an existing annotation, translating a
        //    connector body) the tool wasn't really armed in the menu
        //    sense, so we just cancel the drag and leave tool_ alone.
        if (drag_ != drag_kind::none)
        {
            const bool was_one_shot_drag =
                (drag_ == drag_kind::new_connector ||
                 drag_ == drag_kind::new_text_box);
            if (drag_orig_connector_)
            {
                if (auto* c = annotations_.find_connector(drag_target_id_))
                    *c = *drag_orig_connector_;
            }
            if (drag_orig_text_box_)
            {
                if (auto* tb = annotations_.find_text_box(drag_target_id_))
                    *tb = *drag_orig_text_box_;
            }
            drag_ = drag_kind::none;
            drag_orig_connector_.reset();
            drag_orig_text_box_.reset();
            drag_target_id_  = 0;
            drag_handle_idx_ = 0;
            if (was_one_shot_drag)
            {
                tool_ = tool::none;
                hover_pos_.reset();
            }
            return true;
        }
        // 2. Selection clear.
        if (clear_selection())
            return true;
        // 3. Exit annotation tool.  Lets Esc be a uniform "back out"
        //    key: cancel drag → clear selection → exit tool → and the
        //    host's key handler will then handle "Esc with no
        //    annotation state to clear" (likely a no-op).
        if (tool_ != tool::none)
        {
            set_tool(tool::none);
            return true;
        }
        return false;
    }
    if (key == Qt::Key_Delete || key == Qt::Key_Backspace)
    {
        // Only consume Del/Backspace if we have an annotation
        // selected — otherwise let the host's "delete selected bars"
        // shortcut fire.
        if (selected_text_box_)
        {
            annotations_.remove_text_box(*selected_text_box_);
            selected_text_box_.reset();
            return true;
        }
        if (selected_connector_)
        {
            annotations_.remove_connector(*selected_connector_);
            selected_connector_.reset();
            return true;
        }
        return false;
    }
    return false;
}

// ===========================================================================
// Selection
// ===========================================================================
bool annotation_layer::clear_selection()
{
    if (!selected_text_box_ && !selected_connector_)
        return false;
    selected_text_box_.reset();
    selected_connector_.reset();
    return true;
}

// ===========================================================================
// Cursor
// ===========================================================================
Qt::CursorShape annotation_layer::cursor_for(const QPointF& p) const
{
    // While dragging, cursor stays put as whatever Qt's default is for
    // a press-and-drag (the system's drag cursor).  We don't override.
    if (drag_ != drag_kind::none)
        return Qt::ArrowCursor;

    const hit h = hit_test(p);
    switch (h.k)
    {
    case hit::kind::connector_endpoint:
        return Qt::CrossCursor;
    case hit::kind::text_box_handle:
        // Diagonal vs. horizontal vs. vertical resize cursors.  Index
        // ordering matches text_box_anchors().
        switch (h.handle)
        {
        case 0: case 4: return Qt::SizeFDiagCursor;  // NW, SE
        case 2: case 6: return Qt::SizeBDiagCursor;  // NE, SW
        case 1: case 5: return Qt::SizeVerCursor;    // N,  S
        case 3: case 7: return Qt::SizeHorCursor;    // E,  W
        }
        return Qt::ArrowCursor;
    case hit::kind::connector_body:
    case hit::kind::text_box_body:
        return Qt::SizeAllCursor;
    case hit::kind::none:
        break;
    }
    // Empty canvas with a tool active → crosshair (matches Drawings).
    if (is_connector_tool() || tool_ == tool::text_box)
        return Qt::CrossCursor;
    return Qt::ArrowCursor;
}

// ===========================================================================
// Painting — committed annotations
// ===========================================================================
void annotation_layer::paint(QPainter& painter) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Text boxes first so connectors paint above them.
    for (const auto& tb : annotations_.text_boxes())
        paint_text_box(painter, tb);

    for (const auto& c : annotations_.connectors())
        paint_connector(painter, c);

    painter.restore();
}

void annotation_layer::paint_text_box(QPainter& painter,
                                      const model::text_box& tb) const
{
    const QRectF wr = to_widget(tb.rect);

    // Border policy: by default a text box has NO border — it sits on
    // the chart as if it were native typography.  The 1px gray border
    // only appears when the cursor is over the box, to telegraph "this
    // is a draggable / editable object."  Selected boxes also keep the
    // border visible so the user can see where the selection ends
    // even when the cursor is away.  Same rule Google Drawings uses
    // for borderless text boxes.
    const bool is_selected = selected_text_box_ && *selected_text_box_ == tb.id;
    const bool is_hovered  = hover_pos_.has_value() && wr.contains(*hover_pos_);
    const bool draw_border = is_selected || is_hovered;

    painter.save();
    if (draw_border)
    {
        painter.setPen(QPen(k_text_box_border, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(wr);
    }

    // Text rendering.  Pad inside where the border *would* be by
    // k_text_box_padding so the layout doesn't shift when the border
    // appears/disappears on hover.  Wrap on word boundaries (long
    // words still wrap at character boundaries via WrapAtWordBoundary
    // OrAnywhere if we ever want that — for now WordWrap is fine).
    // Hard newlines (\n) embedded in tb.text are honored as line
    // breaks by QPainter::drawText regardless of the wrap mode, so
    // multi-line content authored in the inline editor renders the
    // same way the editor laid it out.  Align top-left because
    // that's what users expect of a free text box.
    if (!tb.text.isEmpty())
    {
        painter.setPen(k_text_box_text);
        painter.setFont(text_font_);
        QRectF inner = wr.adjusted(k_text_box_padding, k_text_box_padding,
                                   -k_text_box_padding, -k_text_box_padding);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WordWrap);
        opt.setAlignment(Qt::AlignLeft | Qt::AlignTop);
        painter.drawText(inner, tb.text, opt);
    }
    painter.restore();

    // Selection chrome: 8 small filled blue squares on the border at
    // the resize-handle positions, matching Google Drawings.  The
    // square shape (vs. the round anchor dots) is the user's signal
    // that these handles resize the box, rather than serving as
    // attachment points for new connectors — the two roles are
    // visually distinct.  Painted here (committed-annotation pass)
    // rather than in the overlay so the handles stay visible while
    // another drag is in flight (e.g. drawing a connector that snaps
    // to this same box's anchor).
    if (selected_text_box_ && *selected_text_box_ == tb.id)
    {
        const auto pts = text_box_anchors(wr);
        for (const auto& pt : pts)
            paint_square_handle(painter, pt, k_text_box_handle_r,
                                k_selection_color, k_anchor_outline);
    }
}

void annotation_layer::paint_connector(QPainter& painter,
                                       const model::connector& c) const
{
    const QPointF s = resolve_endpoint(c.start);
    const QPointF e = resolve_endpoint(c.end);

    painter.save();
    const bool selected = selected_connector_ && *selected_connector_ == c.id;
    QPen pen(selected ? k_selection_color : k_connector_color,
             selected ? 1.8 : 1.5);
    painter.setPen(pen);
    painter.drawLine(s, e);

    if (c.arrow_at_end)
        paint_arrowhead(painter, e, s);
    if (c.arrow_at_start)
        paint_arrowhead(painter, s, e);

    if (selected)
    {
        paint_dot(painter, s, k_endpoint_handle_r,
                  k_selection_color, k_anchor_outline);
        paint_dot(painter, e, k_endpoint_handle_r,
                  k_selection_color, k_anchor_outline);
    }
    painter.restore();
}

// Arrowhead pointing at `tip`, from the direction of `from`.  Drawn as
// a small filled triangle whose base sits perpendicular to the line.
// Vector math: unit vector along the line, perpendicular by swapping
// and negating one component, then two flank points are tip + base*u ±
// half_w*perp.
void annotation_layer::paint_arrowhead(QPainter& painter,
                                       const QPointF& tip,
                                       const QPointF& from) const
{
    const qreal dx = tip.x() - from.x();
    const qreal dy = tip.y() - from.y();
    const qreal len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-6)
        return;
    const qreal ux = dx / len;
    const qreal uy = dy / len;
    // Base of the arrowhead, k_arrowhead_length back from the tip.
    const QPointF base(tip.x() - ux * k_arrowhead_length,
                       tip.y() - uy * k_arrowhead_length);
    // Perpendicular for the flanks.
    const qreal px = -uy;
    const qreal py =  ux;
    const QPointF flank_a(base.x() + px * k_arrowhead_half_w,
                          base.y() + py * k_arrowhead_half_w);
    const QPointF flank_b(base.x() - px * k_arrowhead_half_w,
                          base.y() - py * k_arrowhead_half_w);
    QPolygonF tri;
    tri << tip << flank_a << flank_b;
    painter.save();
    QColor fill = painter.pen().color();
    painter.setBrush(fill);
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(tri);
    painter.restore();
}

// ===========================================================================
// Painting — overlay (live drag + anchor dots)
// ===========================================================================
void annotation_layer::paint_overlay(QPainter& painter) const
{
    // Three reasons to paint here:
    //   1. A drag is in flight — show rubber-band + all anchors + snap
    //      highlight on the moving endpoint.  This is the
    //      drop-destination affordance.
    //   2. The connector tool is armed and the cursor is hovering near
    //      a shape that has anchors — show those source-side anchors.
    //      Mirrors Google Drawings: pick the line tool, hover a box,
    //      its anchors appear before any click.  The user then clicks
    //      one and the drag starts.
    //   3. Neither — early return, the overlay is empty.
    const bool dragging_endpoint =
        (drag_ == drag_kind::new_connector ||
         drag_ == drag_kind::move_endpoint);
    const bool idle_hover_anchors =
        (drag_ == drag_kind::none &&
         is_connector_tool() &&
         hover_pos_.has_value());

    if (drag_ == drag_kind::none && !idle_hover_anchors)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // --- Anchor dots ---------------------------------------------------
    // For an endpoint drag: show every anchor in the chart and
    // highlight the one we'd snap to right now.  Painting them all
    // tells the user where they can attach — they don't have to
    // discover anchors one shape at a time.
    //
    // For idle hover (case 2): show anchors only on whichever shape
    // the cursor is currently near, mirroring Google Drawings' "hover
    // near a box, see its connection points appear" behavior.  We
    // re-use the same snap_to_anchor() function but with a larger
    // reveal radius so the dots appear when the cursor is *approaching*
    // a shape, not only when it's already inside snap range.
    std::optional<QPointF> snap_target;
    if (dragging_endpoint)
        snap_target = snap_to_anchor(drag_current_);

    if (dragging_endpoint)
    {
        for (const auto& a : all_visible_anchors())
        {
            const bool hovered = snap_target && *snap_target == a;
            paint_dot(painter, a,
                      hovered ? k_anchor_hover_r : k_anchor_dot_r,
                      hovered ? k_anchor_hover_color : k_anchor_color,
                      k_anchor_outline);
        }
    }
    else if (idle_hover_anchors)
    {
        // Find the set of anchors that belong to the shape (text box
        // or line edge) closest to the cursor — only those reveal on
        // idle hover.  Reveal radius is larger than snap radius
        // because the user is "approaching" the shape, not yet
        // committed to attaching.  Once they click and start dragging,
        // the larger anchor set lights up across the whole chart.
        const QPointF p = *hover_pos_;
        const qreal reveal_r2 = k_hover_reveal_radius * k_hover_reveal_radius;

        // Test text boxes first (their 8-point anchor sets are bigger
        // targets), then line edges.  Whichever has any anchor within
        // reveal radius wins; ties go to the most recently drawn (back
        // of the list) by iterating in reverse, same Z-order
        // convention as hit_test.
        std::vector<QPointF> reveal_set;
        const auto& tbs = annotations_.text_boxes();
        for (auto it = tbs.rbegin(); it != tbs.rend() && reveal_set.empty(); ++it)
        {
            const auto pts = text_box_anchors(to_widget(it->rect));
            for (const auto& a : pts)
            {
                const qreal dx = a.x() - p.x();
                const qreal dy = a.y() - p.y();
                if (dx * dx + dy * dy <= reveal_r2)
                {
                    reveal_set.assign(pts.begin(), pts.end());
                    break;
                }
            }
        }
        // Find the snap-radius highlight inside the reveal set, if any
        // — the user gets the same "you're close enough to snap" cue
        // here as during a drag, even though no rubber-band exists yet.
        std::optional<QPointF> hover_snap;
        {
            const qreal snap_r2 = k_snap_radius * k_snap_radius;
            qreal best_d2 = std::numeric_limits<qreal>::max();
            for (const auto& a : reveal_set)
            {
                const qreal dx = a.x() - p.x();
                const qreal dy = a.y() - p.y();
                const qreal d  = dx * dx + dy * dy;
                if (d <= snap_r2 && d < best_d2)
                {
                    best_d2 = d;
                    hover_snap = a;
                }
            }
        }
        for (const auto& a : reveal_set)
        {
            const bool hovered = hover_snap && *hover_snap == a;
            paint_dot(painter, a,
                      hovered ? k_anchor_hover_r : k_anchor_dot_r,
                      hovered ? k_anchor_hover_color : k_anchor_color,
                      k_anchor_outline);
        }
    }

    // Render the rubber-band for each drag kind.
    switch (drag_)
    {
    case drag_kind::new_connector:
    case drag_kind::move_endpoint:
    {
        // Google Drawings draws the live line as a solid stroke in the
        // line's final color, not a dashed indicator — the rubber-band
        // looks the same as the committed line will look, just shorter
        // and changing as the cursor moves.  Use k_connector_color
        // (not the selection blue) so what the user sees during the
        // drag matches what they get on commit.
        const QPointF end = snap_target.value_or(drag_current_);
        painter.setPen(QPen(k_connector_color, 1.5));
        painter.drawLine(drag_anchor_, end);
        // Live arrowhead in the same color, so direction is obvious.
        paint_arrowhead(painter, end, drag_anchor_);
        break;
    }
    case drag_kind::translate_connector:
    {
        // Translation ghost stays dashed because the *original* line
        // is still painted in its solid form behind this ghost — the
        // user is comparing "where it was" (solid) with "where it'll
        // land" (dashed).  Distinct styles keep them separable.
        // Glued endpoints don't translate (see the corresponding
        // mouse_release case), so we add the delta only to free ones
        // when rendering the ghost — the user sees an accurate
        // preview of what the release will produce.
        if (drag_orig_connector_)
        {
            const QPointF delta = drag_current_ - drag_anchor_;
            QPointF s = resolve_endpoint(drag_orig_connector_->start);
            QPointF e = resolve_endpoint(drag_orig_connector_->end);
            if (drag_orig_connector_->start.k ==
                model::connector_endpoint::kind::free)
                s += delta;
            if (drag_orig_connector_->end.k ==
                model::connector_endpoint::kind::free)
                e += delta;
            QPen pen(k_selection_color, 1.5);
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.drawLine(s, e);
        }
        break;
    }
    case drag_kind::new_text_box:
    {
        QRectF r = QRectF(drag_anchor_, drag_current_).normalized();
        QPen pen(k_selection_color, 1.0);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);
        break;
    }
    case drag_kind::move_text_box:
    {
        if (drag_orig_text_box_)
        {
            const QPointF delta = drag_current_ - drag_anchor_;
            QRectF r = to_widget(drag_orig_text_box_->rect);
            r.translate(delta);
            QPen pen(k_selection_color, 1.0);
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(r);
        }
        break;
    }
    case drag_kind::resize_text_box:
    {
        // Reconstruct the new rect the same way mouse_release will:
        // from drag_anchor_ (pinned opposite corner) and the current
        // cursor, with single-axis constraint for edge-midpoint handles.
        QPointF new_pt = drag_current_;
        if (drag_orig_text_box_)
        {
            const QRectF wr = to_widget(drag_orig_text_box_->rect);
            if (drag_handle_idx_ == 1 || drag_handle_idx_ == 5)
                new_pt.setX(drag_anchor_.x() == wr.left() ? wr.right() : wr.left());
            else if (drag_handle_idx_ == 3 || drag_handle_idx_ == 7)
                new_pt.setY(drag_anchor_.y() == wr.top() ? wr.bottom() : wr.top());
        }
        QRectF r = QRectF(drag_anchor_, new_pt).normalized();
        QPen pen(k_selection_color, 1.0);
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);
        break;
    }
    case drag_kind::none:
        break;
    }
    painter.restore();
}

void annotation_layer::paint_dot(QPainter& painter,
                                 const QPointF& pos,
                                 qreal radius,
                                 const QColor& fill,
                                 const QColor& outline) const
{
    painter.save();
    painter.setPen(QPen(outline, 1.0));
    painter.setBrush(fill);
    painter.drawEllipse(pos, radius, radius);
    painter.restore();
}

// Filled square with a white outline ring, centered on `pos`.  Used
// for text-box resize handles, where Google Drawings uses filled blue
// squares (distinct from the round anchor dots which signal "you
// could attach a line here" rather than "you could resize me here").
// The half_size argument is the half-width — so a 6px-tall handle is
// half_size=3.0.
void annotation_layer::paint_square_handle(QPainter& painter,
                                           const QPointF& pos,
                                           qreal half_size,
                                           const QColor& fill,
                                           const QColor& outline) const
{
    painter.save();
    painter.setPen(QPen(outline, 1.0));
    painter.setBrush(fill);
    painter.drawRect(QRectF(pos.x() - half_size, pos.y() - half_size,
                            half_size * 2.0, half_size * 2.0));
    painter.restore();
}

} // namespace nashville::view
