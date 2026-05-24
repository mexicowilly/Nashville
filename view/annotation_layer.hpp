#pragma once

#include "../model/annotations.hpp"
#include "layout_structs.hpp"
#include <QPointF>
#include <QRectF>
#include <QFont>
#include <Qt>
#include <vector>
#include <optional>
#include <functional>
#include <array>
#include <cstdint>

class QPainter;

namespace nashville::view
{

// Owns the placement, interaction, and painting of free-floating
// annotations (text boxes and straight connector lines) on top of the
// song chart.  Composed into song_body_widget; receives forwarded mouse
// and key events when the annotation tool is active (or when the click
// lands on an annotation, even in bar mode — see the routing in
// song_body_widget::mousePressEvent).
//
// The "Google Drawings"-style behavior the user asked for is realised
// here, not in the data model: anchors (small dots on text boxes and on
// line-of-bars edges) appear only during an active endpoint drag,
// snapping is applied to the dragged endpoint within k_snap_radius, and
// on commit the endpoint is stored as a bare QPointF — no glue, no
// reflow-follow.  See the design doc the user landed on.
//
// Coordinate space: the model stores song-local coords, with origin at
// the chart-content top-left (the same origin compute_layout uses
// internally inside song_body_widget).  All conversion happens at this
// layer's boundary via the chart_content_rect_ supplier the widget
// hands to us.
class annotation_layer
{
public:
    // The lines reference must remain valid for the layer's lifetime;
    // song_body_widget owns both the layer and lines_ so this is fine
    // in practice.  chart_content_rect is a supplier (not a value)
    // because the rect changes on every resize/layout and we don't want
    // to plumb resize callbacks through.
    annotation_layer(model::annotations& anns,
                     const std::vector<line_layout>& lines,
                     std::function<QRectF()> chart_content_rect);

    // --- Tool mode -------------------------------------------------------
    // Tool modes.  `line` and `arrow` are both connector tools — they
    // differ only in whether the freshly-drawn connector has an
    // arrowhead on its end.  Splitting them at the tool level lets the
    // Insert menu present "Line" and "Arrow" as separate user-facing
    // options without bifurcating the connector data model.  Both tools
    // route through the same drag state machine; the creation step in
    // mouse_release reads the active tool and stamps the right
    // arrow_at_end value on the new connector.  Endpoint-move and body-
    // translate drags don't care which tool is active — they preserve
    // the existing connector's arrowheads.
    enum class tool { none, text_box, line, arrow };
    void set_tool(tool t);                  // clears selection AND any in-flight drag
    tool current_tool() const { return tool_; }

    // --- Painting --------------------------------------------------------
    // Painted by song_body_widget::paintEvent AFTER the bars and BEFORE
    // the live drag overlay.  Order within annotations is: text boxes
    // first, then connectors above them (so an arrow terminating at a
    // text box doesn't get hidden inside the box's fill).
    void paint(QPainter& painter) const;

    // Painted last in the paintEvent.  Draws the rubber-band line or
    // box, the visible anchor dots during an endpoint drag, the
    // highlighted hovered anchor (if any), and selection chrome
    // (endpoint handles for a selected connector, resize handles for a
    // selected text box).  Empty no-op when nothing is happening.
    void paint_overlay(QPainter& painter) const;

    // --- Event forwarding ------------------------------------------------
    // Each returns true iff the event was consumed.  song_body_widget
    // forwards events to these in a specific order documented in its
    // mousePressEvent et al; in short, the layer claims:
    //
    //   * Any click in connector or text_box tool mode.
    //   * In bar mode: clicks that land on an existing annotation
    //     (selecting / dragging it).  This is so the user can interact
    //     with the annotations they've already placed without first
    //     switching tools — matches Google Drawings, where you don't
    //     need to be in "line mode" to drag a line you previously drew.
    //
    // Position is in widget coordinates (the same coords as QMouseEvent
    // delivers).
    bool mouse_press(const QPointF& widget_pos,
                     Qt::MouseButton button,
                     Qt::KeyboardModifiers mods);
    bool mouse_move(const QPointF& widget_pos);
    bool mouse_release(const QPointF& widget_pos);
    bool mouse_double_click(const QPointF& widget_pos);

    // Returns true if a hover-state change since the last call needs
    // a repaint that mouse_move() didn't already consume.  The host
    // calls this AFTER its own hover-handling code and update()s once
    // if true.  Specifically: when the cursor crosses a text-box
    // boundary (entering or exiting), the border on that box appears
    // or disappears, and that visual change has to be painted even if
    // we're in bar mode and mouse_move returned false.  Read-and-
    // clear semantics so it's a one-shot signal — the next move event
    // will set it again if state changed again.
    bool take_hover_repaint();

    // Called when the cursor leaves the widget.  Clears idle-hover
    // state so we don't keep painting anchor dots over a position the
    // user is no longer pointing at.  Doesn't touch drag state — a
    // drag survives the cursor briefly exiting the widget (Qt
    // delivers move events even outside the widget while a button is
    // held).
    void mouse_leave();

    // Esc: cancel any in-flight drag, then if nothing was in flight,
    // clear selection, then if nothing was selected, fall through (the
    // caller will then also fall through to bar deselection / tool exit
    // behavior).  Del/Backspace: delete the selected annotation if any.
    // Returns true if consumed.
    bool key_press(int key, Qt::KeyboardModifiers mods);

    // --- Selection & cursor ---------------------------------------------
    bool has_selection() const
    {
        return selected_text_box_.has_value() || selected_connector_.has_value();
    }
    // Returns true iff the selection actually changed.
    bool clear_selection();

    // What cursor should the host widget show at this position, given
    // the current tool and what (if anything) is under the cursor?
    // Returned as Qt::CursorShape rather than a QCursor so song_body_
    // widget can blend this with its own cursor decisions (divider,
    // bars, etc.) without us needing to know about them.
    Qt::CursorShape cursor_for(const QPointF& widget_pos) const;

    // For the host's paintEvent to know whether to skip its own hover
    // outlines (e.g. insertion-slot dashed boxes) while we're in an
    // annotation tool — those affordances are distracting when the user
    // is placing annotations.
    bool tool_active() const { return tool_ != tool::none; }

    // --- Text-box editing hook ------------------------------------------
    // The layer doesn't own a QLineEdit (song_body_widget already has
    // the open_line_editor / close_line_editor plumbing, with its
    // QLineEdit-as-overlay machinery, Esc filter, etc.).  Instead we
    // call back to the host when we need to edit a text box's text.
    // Set this in the constructor wiring inside song_body_widget.
    //
    // The callback receives the text box's id and the widget-coord rect
    // to position the editor over.  The host opens its inline editor
    // and, on commit, applies the new text to the annotation by id.
    using edit_text_callback =
        std::function<void(std::uint64_t text_box_id, const QRectF& widget_rect)>;
    void set_edit_text_callback(edit_text_callback cb)
    {
        edit_text_ = std::move(cb);
    }

    // Font used to render text-box contents.  song_body_widget sets this
    // from its own font setup so annotations match the chart's look.
    void set_text_font(const QFont& f) { text_font_ = f; }

private:
    // True iff the current tool creates connectors (line or arrow).
    // Used in several places that previously checked `tool_ ==
    // tool::connector` and should now accept both connector-flavor
    // tools.  Inline so the compiler folds it into branch tests.
    bool is_connector_tool() const
    {
        return tool_ == tool::line || tool_ == tool::arrow;
    }

    // --- Coordinate conversion ------------------------------------------
    QPointF to_widget(const QPointF& song_pt) const;
    QRectF  to_widget(const QRectF&  song_rect) const;
    QPointF to_song(const QPointF& widget_pt) const;

    // Resolve a connector endpoint to its current widget-coordinate
    // position.  For a free endpoint that's just to_widget(free_pos).
    // For a text-box-anchored endpoint, it looks up the text box by
    // id and returns the anchor's current widget position; the anchor
    // tracks the box as the box is moved or resized, which is the
    // whole point of gluing in the first place.  If the referenced
    // text box no longer exists (defensive — annotations::remove_text_box
    // converts glued endpoints to free before the box is erased, so
    // this should never happen in practice), we return (0, 0) and
    // log; better that than crash.
    QPointF resolve_endpoint(const model::connector_endpoint& ep) const;

    // The inverse: given a widget-coord cursor position and the snap
    // target (if any), build a connector_endpoint of the right kind.
    // If the snap target is a text-box anchor we return an anchored
    // endpoint; otherwise free at the cursor position (snap_to_anchor
    // for line-edge anchors returns a position but no text_box id —
    // see the implementation of snap_to_anchor for the discriminator
    // we read here).
    model::connector_endpoint make_endpoint(const QPointF& widget_pos) const;

    // --- Anchor enumeration ---------------------------------------------
    // The 8 anchors on a text box, in widget coords.  Ordering is
    // [NW, N, NE, E, SE, S, SW, W] so even and odd indices alternate
    // corners and edge-midpoints; this only matters for resize-handle
    // assignment, not for snap (snap considers all 8 uniformly).
    static std::array<QPointF, 8> text_box_anchors(const QRectF& widget_rect);

    // Every anchor visible during the current endpoint drag, in widget
    // coords.  Includes the 8 anchors per text box in the model.  Line
    // -of-bars edges used to advertise as anchors here too, but they
    // don't glue (bar rows reflow) and we removed them — only text-box
    // anchors are visible as snap targets now.
    std::vector<QPointF> all_visible_anchors() const;

    // Rich snap result.  We need to know not just the snapped position
    // but also whether it came from a text-box anchor (so creation /
    // endpoint-move can glue to it) or from a line-of-bars anchor or
    // empty canvas (in which case the endpoint stays free).
    struct snap_result
    {
        QPointF pos;                     // widget coords
        bool    is_text_box_anchor = false;
        std::uint64_t text_box_id  = 0;  // valid iff is_text_box_anchor
        unsigned      anchor_index = 0;  // 0..7
    };
    std::optional<snap_result> snap_at(const QPointF& widget_pt) const;

    // Convenience: same query, position-only, for the old call sites
    // that just need to know "should the rubber-band visually snap?"
    // without caring about the discriminator.  Forwards to snap_at.
    std::optional<QPointF> snap_to_anchor(const QPointF& widget_pt) const;

    // --- Hit-testing committed annotations ------------------------------
    struct hit
    {
        enum class kind
        {
            none,
            text_box_body,       // body interior — drag/select
            text_box_handle,     // one of 8 resize handles (selected only)
            connector_body,      // line segment — drag/select
            connector_endpoint,  // endpoint handle (selected only)
        };
        kind k = kind::none;
        std::uint64_t id = 0;
        // For text_box_handle: 0..7 anchor index (same ordering as
        // text_box_anchors).  For connector_endpoint: 0 = start, 1 = end.
        unsigned handle = 0;
    };
    // Topmost-first.  Tests connector endpoint handles → connector
    // bodies → text-box handles → text-box bodies, in that priority.
    hit hit_test(const QPointF& widget_pos) const;

    // --- Painting helpers -----------------------------------------------
    void paint_text_box(QPainter& painter, const model::text_box& tb) const;
    void paint_connector(QPainter& painter, const model::connector& c) const;
    void paint_arrowhead(QPainter& painter,
                         const QPointF& tip,
                         const QPointF& from) const;
    // Endpoint / resize / anchor dots, all rendered the same way at
    // different radii via this single helper.
    void paint_dot(QPainter& painter,
                   const QPointF& pos,
                   qreal radius,
                   const QColor& fill,
                   const QColor& outline) const;
    // Filled square handle (used for text-box resize handles, which
    // Google Drawings draws as squares to differentiate them from the
    // round connection-point dots and round endpoint handles).
    void paint_square_handle(QPainter& painter,
                             const QPointF& pos,
                             qreal half_size,
                             const QColor& fill,
                             const QColor& outline) const;

    // --- Geometry helpers -----------------------------------------------
    static qreal point_to_segment_distance(const QPointF& p,
                                           const QPointF& a,
                                           const QPointF& b);

    // --- Drag state machine ---------------------------------------------
    enum class drag_kind
    {
        none,
        new_connector,         // creating from scratch (anchored end = start)
        move_endpoint,         // dragging an existing endpoint
        translate_connector,   // dragging the connector body
        new_text_box,          // rubber-banding a new box
        move_text_box,         // dragging an existing box
        resize_text_box,       // dragging one of 8 handles
    };
    drag_kind drag_ = drag_kind::none;

    // Interpretation depends on drag_, documented at the declaration in
    // annotation_layer.hpp's design discussion.  All in widget coords.
    QPointF drag_anchor_;
    QPointF drag_current_;

    // For new_connector specifically: the endpoint kind+id captured at
    // press time, so the *start* side of the freshly-created connector
    // can be glued to a text-box anchor if the user started the drag
    // on one.  We capture this once at press because the start side
    // doesn't change during the drag (only the end side follows the
    // cursor), and we can't re-derive it on release because the cursor
    // has moved.  Free endpoint at drag_anchor_ in song coords is the
    // common case; the only time this is anchored is when the press
    // landed within snap radius of a text-box anchor.
    model::connector_endpoint drag_start_endpoint_;

    // For drags that mutate an existing annotation: the target id and,
    // where applicable, which handle / endpoint.  For new_connector and
    // new_text_box these stay 0.
    std::uint64_t drag_target_id_ = 0;
    unsigned      drag_handle_idx_ = 0;

    // Snapshot of the original geometry so Esc-cancel during a mutating
    // drag can restore it.  Set in mouse_press for move/resize/translate;
    // unused for new_* drags (cancel just discards the in-flight rect).
    std::optional<model::text_box>  drag_orig_text_box_;
    std::optional<model::connector> drag_orig_connector_;

    // Cursor position last seen while the connector tool was armed and
    // no drag was in flight.  Used by paint_overlay to figure out which
    // shape the user is hovering near, so it can show source-side
    // anchor dots on that shape (Google Drawings behavior: arm the line
    // tool, hover near a box, the box reveals its anchors *before* you
    // click).  Also used by paint() to drive border-on-hover for text
    // boxes in *any* tool mode (Google's "no border until you hover"
    // text-box style).  nullopt means "cursor has left the widget" —
    // no idle-hover anchors and no hover borders in that case.
    std::optional<QPointF> hover_pos_;

    // Set by mouse_move when the cursor crosses a text-box boundary
    // in a tool that doesn't consume moves (bar mode, text-box tool).
    // The host polls this via take_hover_repaint() after handling its
    // own hover updates and triggers a repaint if true.  See the
    // detailed comment on mouse_move for the consume-vs-repaint
    // distinction.
    bool hover_repaint_pending_ = false;

    // --- Selection ------------------------------------------------------
    // At most one annotation selected at a time (v1 — multi-select can
    // come later if there's demand).  The two optionals are mutually
    // exclusive; clear_selection() resets both.
    std::optional<std::uint64_t> selected_text_box_;
    std::optional<std::uint64_t> selected_connector_;

    // --- Refs / state ---------------------------------------------------
    model::annotations&             annotations_;
    // lines_ is held but currently unread.  It used to back the
    // line-of-bars edge anchors, which we removed because those
    // edges don't glue (bar rows reflow on resize / bars_per_line
    // change) and advertising them as anchors was misleading.  Kept
    // as a reference in case a future affordance (e.g. snapping a
    // text box's top edge to a line's baseline) wants it back; the
    // cost of a const reference is nil and the constructor's
    // wiring in song_body_widget stays stable.
    const std::vector<line_layout>& lines_;
    std::function<QRectF()>         chart_content_rect_;
    tool                            tool_ = tool::none;

    edit_text_callback              edit_text_;
    QFont                           text_font_;

    // --- Tunables (pixels, widget coords) -------------------------------
    // k_snap_radius is the snap-or-float threshold for an endpoint drag.
    // k_endpoint_hit_r is larger than the visual handle radius so the
    // handle is easy to grab; same idea for the body tolerance.
    // k_anchor_dot_r / k_anchor_hover_r are the visual sizes of the
    // anchor dots during an endpoint drag.
    static constexpr qreal k_snap_radius        = 10.0;
    // Reveal radius is bigger than snap radius: dots *appear* when the
    // cursor is approaching a shape (16px), and snap when it's clearly
    // close enough to attach (10px).  This two-stage hysteresis is
    // what Google Drawings does — anchors visible from farther away
    // than the actual snap zone, so you can see where you'd land
    // before you commit.
    static constexpr qreal k_hover_reveal_radius = 16.0;
    static constexpr qreal k_endpoint_handle_r  = 4.0;
    static constexpr qreal k_endpoint_hit_r     = 10.0;
    static constexpr qreal k_body_hit_tolerance = 5.0;
    static constexpr qreal k_anchor_dot_r       = 3.5;
    static constexpr qreal k_anchor_hover_r     = 5.5;
    static constexpr qreal k_text_box_handle_r  = 3.5;
    static constexpr qreal k_text_box_hit_r     = 8.0;
    static constexpr qreal k_text_box_padding   = 4.0;
    static constexpr qreal k_arrowhead_length   = 10.0;
    static constexpr qreal k_arrowhead_half_w   = 4.0;
};

} // namespace nashville::view
