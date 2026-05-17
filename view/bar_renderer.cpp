#include "bar_renderer.hpp"
#include <QFontMetricsF>
#include <QPainterPath>
#include <cmath>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// Repeat-mark geometry
// ---------------------------------------------------------------------------
// These constants describe the painted glyph (NOT the slot width — see
// k_repeat_slot_w in the header).  The thick bar carries the visual
// weight; the thin bar mirrors a standard barline; the dots sit between
// the two bars on the side facing the music being repeated.  Wings hook
// outward from the thick bar in the same direction as the dots; they
// substitute for the staff lines that would normally anchor the dots'
// vertical position in conventional notation.
namespace {
constexpr qreal k_thick_w   = 3.0;   // thick vertical bar
constexpr qreal k_thin_w    = 1.0;   // thin vertical bar
constexpr qreal k_bars_gap  = 3.0;   // gap between thick and thin
constexpr qreal k_dot_r     = 1.6;   // dot radius
constexpr qreal k_dot_inset = 4.0;   // dot offset from the bar's centre
constexpr qreal k_wing_len  = 6.0;   // length of each wing stroke
constexpr qreal k_wing_dy   = 3.0;   // vertical drop from bar end to wing tip
}

// ---------------------------------------------------------------------------
// Public: width_hint
// ---------------------------------------------------------------------------
qreal bar_renderer::width_hint(const model::bar& bar,
                              qreal /*height*/,
                              const chord_renderer::Fonts& fonts,
                              bool draw_begin_repeat,
                              bool draw_end_repeat)
{
    if (bar.empty())
        return 0.0;

    qreal total_width = 0.0;
    for (const auto& ch : bar.chords())
        total_width += chord_renderer::size_hint(ch, fonts).width();

    total_width += k_inter_chord_spacing * (bar.chords().size() - 1);
    total_width += k_time_sig_slot_w;

    // Repeat marks each consume a dedicated slot so the chord row never
    // ends up sharing horizontal space with the dots and wings.  Width
    // is identical on both sides for visual symmetry.
    if (draw_begin_repeat) total_width += k_repeat_slot_w;
    if (draw_end_repeat)   total_width += k_repeat_slot_w;

    return total_width;
}

// ---------------------------------------------------------------------------
// Public: number_row_center_y
// ---------------------------------------------------------------------------
// Mirrors the chord_renderer numRect computation so external callers can
// align adornments (notably the inter-bar continuation dot) with the
// chord-number row.  Any change to top_pad, k_plain_top_pad, or the
// chord-slot height formula in paint() must be reflected here too — the
// two pieces of code share the same geometry contract.
qreal bar_renderer::number_row_center_y(const QRectF& rect,
                                       const model::bar& bar,
                                       const chord_renderer::Fonts& fonts,
                                       bool line_duration_mode,
                                       bool line_has_articulation)
{
    constexpr qreal top_pad         = 2.0;  // matches paint()
    constexpr qreal k_plain_top_pad = 4.0;  // matches chord_renderer

    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF artFm(fonts.articulation);

    qreal num_h = nmFm.ascent() + nmFm.descent();
    qreal art_h = line_has_articulation
                  ? artFm.ascent() + artFm.descent()
                  : 0.0;

    // Does any chord in this bar have a diamond?  Only relevant for
    // duration-mode bars — in plain mode the chord slot fills the bar
    // minus top_pad, with no diamond-dependent height.
    bool bar_has_diamond = false;
    for (const auto& ch : bar.chords())
        if (ch.is_diamond()) { bar_has_diamond = true; break; }

    qreal chord_slot_top = line_duration_mode
                           ? rect.top()
                           : rect.top() + top_pad;
    qreal chord_slot_h   = line_duration_mode
                           ? num_h + art_h + (bar_has_diamond
                                 ? chord_renderer::k_diamond_padding_v + 1.0 : 0.0)
                           : rect.height() - top_pad;

    qreal top_offset = line_has_articulation ? art_h : k_plain_top_pad;

    qreal num_top = chord_slot_top + top_offset;
    qreal num_h_actual = chord_slot_h - top_offset;
    return num_top + num_h_actual / 2.0;
}

// ---------------------------------------------------------------------------
// Public: paint
// ---------------------------------------------------------------------------
void bar_renderer::paint(QPainter& painter,
                         const QRectF& rect,
                         const model::bar& bar,
                         const chord_renderer::Fonts& fonts,
                         bool line_duration_mode,
                         bool line_has_articulation,
                         bool draw_begin_repeat,
                         bool draw_end_repeat)
{
    if (bar.empty())
        return;

    bool duration_mode = is_duration_mode(bar);

    constexpr qreal top_pad = 2.0;  // fixed top pad, independent of line height

    // For duration-mode lines the chord slot must be tall enough to hold the
    // number glyph plus the articulation zone (if any).  Using a fixed ratio of
    // the bar height fails once art height is added to the bar.  Compute it
    // directly from font metrics instead so it is always exact.
    QFontMetricsF nmFm_slot(fonts.number);
    QFontMetricsF artFm_slot(fonts.articulation);
    qreal num_h = nmFm_slot.ascent() + nmFm_slot.descent();
    qreal art_h = line_has_articulation ? artFm_slot.ascent() + artFm_slot.descent() : 0.0;

    // Does any chord on this line have a diamond? If so the diamond's bottom
    // point extends k_diamond_padding_v below the tight glyph rect.  Add that
    // plus 1px clearance to the chord slot so the rule always clears the diamond.
    bool line_has_diamond = false;
    for (const auto& ch : bar.chords())
        if (ch.is_diamond()) { line_has_diamond = true; break; }

    qreal chord_slot_h  = line_duration_mode
                         ? num_h + art_h + (line_has_diamond
                               ? chord_renderer::k_diamond_padding_v + 1.0 : 0.0)
                         : rect.height() - top_pad;
    constexpr qreal k_rhythm_row_px = 16.0;  // must match duration_bar_height
    qreal rhythm_row_h = line_duration_mode ? k_rhythm_row_px : rect.height() * k_rhythm_row_ratio;

    // --- Layout: reserve slots for repeat marks first ---
    // The repeat-mark slots sit at the bar's outer edges, OUTSIDE the
    // time-signature slot's left position — i.e. the begin-repeat is
    // the leftmost element of the bar, the end-repeat the rightmost.
    // Reserving them up-front keeps the chord-row math independent of
    // whether repeat marks are drawn (they just shrink the available
    // chord-row width on the affected sides).
    qreal begin_repeat_left = rect.left();
    qreal end_repeat_right  = rect.right();
    qreal interior_left  = rect.left()  + (draw_begin_repeat ? k_repeat_slot_w : 0.0);
    qreal interior_right = rect.right() - (draw_end_repeat   ? k_repeat_slot_w : 0.0);

    // --- Time signature: always reserve k_time_sig_slot_w so chord columns align ---
    // Paint glyphs only when this bar actually has a time sig change.
    qreal chords_left = interior_left + k_time_sig_slot_w;

    if (bar.time_sig())
    {
        const auto& ts = *bar.time_sig();
        QFont tsFont = fonts.modifier;
        QFontMetricsF fm(tsFont);

        QString countStr = QString::number(ts.count());
        QString kindStr  = QString::number(static_cast<int>(ts.kind()));
        qreal max_adv = std::max(fm.horizontalAdvance(countStr),
                                fm.horizontalAdvance(kindStr));
        qreal ts_x    = interior_left + (k_time_sig_slot_w - max_adv) / 2.0;  // centred in slot
        qreal ts_y    = rect.top() + top_pad;
        qreal sep     = 3.0;

        // Numerator baseline = ts_y + ascent
        painter.save();
        painter.setFont(tsFont);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawText(QPointF(ts_x + (max_adv - fm.horizontalAdvance(countStr)) / 2.0,
                                 ts_y + fm.ascent()),
                         countStr);
        painter.restore();

        // Separator: sits sep px below the bottom of the numerator glyph
        qreal line_y = std::round(ts_y + fm.ascent() + fm.descent() + sep);
        painter.fillRect(QRectF(ts_x, line_y, max_adv, 1.0), Qt::black);

        // Denominator baseline = line_y + sep + ascent
        painter.save();
        painter.setFont(tsFont);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawText(QPointF(ts_x + (max_adv - fm.horizontalAdvance(kindStr)) / 2.0,
                                 line_y + sep + fm.ascent()),
                         kindStr);
        painter.restore();
    }

    QRectF chord_slot_rect(chords_left,
                          line_duration_mode ? rect.top() : rect.top() + top_pad,
                          interior_right - chords_left,
                          chord_slot_h);

    qreal line_y = chord_slot_rect.bottom();

    // Compute per-chord widths
    std::vector<qreal> chord_widths;
    qreal total_chord_width = 0.0;
    for (const auto& ch : bar.chords())
    {
        qreal w = chord_renderer::size_hint(ch, fonts).width();
        chord_widths.push_back(w);
        total_chord_width += w;
    }

    qreal available_width = chord_slot_rect.width()
                           - k_inter_chord_spacing * (bar.chords().size() - 1);
    qreal scale = (total_chord_width > 0.0) ? available_width / total_chord_width : 1.0;

    qreal x = chords_left;
    qreal last_chord_right = chords_left;
    for (std::size_t i = 0; i < bar.chords().size(); ++i)
    {
        qreal slot_width = chord_widths[i] * scale;
        QRectF slot_rect(x, chord_slot_rect.top(), slot_width, chord_slot_h);

        // Pass line_has_articulation normally — chord_renderer carves the art
        // zone from the top of the (now correctly sized) chord slot.
        chord_renderer::paint(painter, slot_rect, bar.chords()[i], fonts, duration_mode,
                              line_has_articulation);

        last_chord_right = x + chord_widths[i];  // actual glyph right edge

        if (duration_mode && line_duration_mode)
        {
            QRectF rhythm_rect(x, line_y + 1.0, slot_width, rhythm_row_h);
            chord_renderer::paint_rhythm(painter, rhythm_rect, bar.chords()[i], fonts);
        }

        x += slot_width + k_inter_chord_spacing;
    }

    // Single underline from chords-left to just past the last chord glyph
    if (bar.chords().size() > 1)
    {
        constexpr qreal k_underline_overhang = 4.0;
        painter.fillRect(QRectF(chords_left,
                                line_y,
                                last_chord_right + k_underline_overhang - chords_left,
                                1.0),
                         Qt::black);
    }

    // Bar-wide horizontal rule for duration-mode bars — same weight as the
    // multi-chord underline so they appear visually identical.
    if (duration_mode && line_duration_mode)
    {
        // Identical to the multi-chord underline: crisp 1px black fillRect.
        painter.fillRect(QRectF(chords_left, line_y,
                                chord_slot_rect.width(), 1.0),
                         Qt::black);
    }

    // --- Repeat marks ---
    // Painted last so they sit on top of any rule/underline strokes.
    //
    // Vertically the mark spans just the chord-number glyph height,
    // centred on the number row.  Articulations above the row (push
    // arrow, staccato dot, tied-into arc) and anything below the row
    // (split-bar underline, duration-mode rhythm row) are explicitly
    // OUTSIDE the visual scope of the repeat — those elements belong
    // to individual chords, not to the bar's repeat boundary.  The
    // wings (drawn by paint_repeat_mark) flare outward from the bar
    // ends but stay close enough that they don't visually invade the
    // articulation row either.
    if (draw_begin_repeat || draw_end_repeat)
    {
        QFontMetricsF nmFm(fonts.number);
        qreal num_h_repeat  = nmFm.ascent() + nmFm.descent();
        qreal centre_y      = number_row_center_y(rect, bar, fonts,
                                                  line_duration_mode,
                                                  line_has_articulation);
        qreal mark_top      = centre_y - num_h_repeat / 2.0;
        qreal mark_bottom   = centre_y + num_h_repeat / 2.0;
        qreal mark_h        = mark_bottom - mark_top;

        if (draw_begin_repeat)
        {
            QRectF mark_rect(begin_repeat_left, mark_top,
                             k_repeat_slot_w, mark_h);
            paint_repeat_mark(painter, mark_rect, /*is_begin=*/true);
        }
        if (draw_end_repeat)
        {
            QRectF mark_rect(end_repeat_right - k_repeat_slot_w, mark_top,
                             k_repeat_slot_w, mark_h);
            paint_repeat_mark(painter, mark_rect, /*is_begin=*/false);
        }
    }
}

// ---------------------------------------------------------------------------
// Private: paint_repeat_mark
// ---------------------------------------------------------------------------
// Nashville charts have no staff to anchor the conventional barline-dot-
// dot pattern, so the dots' vertical position can read as floating.  The
// little angled "wings" hooking off the thick bar fill that gap — they
// signal the boundaries of the repeat the way the top and bottom staff
// lines would in conventional notation.  Each wing flares OUTWARD from
// the bar end: the top wing rises up-and-out (above the bar's top), the
// bottom wing falls down-and-out (below the bar's bottom).  The wings
// flare in the same direction as the dots (toward the music being
// repeated).
//
// NOTE: The wings still have small join issues with the thick bar at
// their anchor point and the bar may show a sliver of itself outside
// the wing.  This is a known visual issue to be revisited.  The
// previous polygon-based approach (with wings as integrated chamfers
// of the bar) eliminated the join issue but produced a different
// visual that was rejected as a regression.
//
// Geometry (begin-repeat, mirrored for end):
//
//          /  ┃ ┃          ▲ top wing: rises up-and-right, away from
//         /   ┃ ┃            the bar's top end
//             ┃ ┃ thin
//             ┃ ┃│ • (dot)
//        thick┃ ┃│
//             ┃ ┃│ • (dot)
//             ┃ ┃
//         \   ┃ ┃          ▼ bottom wing: falls down-and-right, away
//          \  ┃ ┃            from the bar's bottom end
//
void bar_renderer::paint_repeat_mark(QPainter& painter,
                                     const QRectF& mark_rect,
                                     bool is_begin)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Centre the two-bar block horizontally in the slot.
    qreal bars_total_w = k_thick_w + k_bars_gap + k_thin_w;
    qreal block_left   = mark_rect.left() + (mark_rect.width() - bars_total_w) / 2.0;

    qreal thick_x, thin_x;
    if (is_begin)
    {
        // Begin-repeat: thick bar to the left of the thin bar, dots and
        // wings face into the music (to the right).
        thick_x = block_left;
        thin_x  = block_left + k_thick_w + k_bars_gap;
    }
    else
    {
        // End-repeat: mirror image — thin bar to the left of the thick.
        thin_x  = block_left;
        thick_x = block_left + k_thin_w + k_bars_gap;
    }

    qreal top    = mark_rect.top();
    qreal bottom = mark_rect.bottom();

    // --- Wings (painted BEFORE the bars) ---
    // The clean-join problem: a stroked diagonal line meeting the
    // corner of a filled rectangle inevitably leaves a tiny visual
    // seam — either a step at the bar's corner (if the stroke's
    // outer edge starts at the corner but the stroke itself sits
    // beside the bar), or a kink (if the stroke crosses the bar's
    // edge at the rasteriser-centred mid-line).
    //
    // We resolve it by painting the wings first, with the path
    // anchored slightly INSIDE the bar's outline.  The bar's filled
    // rect is then painted on top and crisply masks the portion of
    // the wing stroke that lies within the bar — so the visible part
    // of each wing is exactly its outer extent, emerging cleanly from
    // the bar's edge.
    //
    // Path geometry: the stroke's OUTER edge passes through the bar's
    // corner point (otherwise the bar's corner sticks up beyond where
    // the wing emerges).  Achieved by computing a perpendicular
    // displacement (towards the centre of the repeat mark) of
    // magnitude k_half_pen, then extending the path tail backward
    // along the wing's inverse outward direction so the stroke
    // definitely overlaps into the bar's body for masking.
    //
    // The perpendicular always points (a) along the bar edge the
    // wing emerges from (so for begin: positive X; for end: negative
    // X) and (b) towards the mark's vertical centre (so for top:
    // positive Y; for bottom: negative Y).  Its components are
    //   perp_x = x_sign * |outward_y|
    //   perp_y = y_sign * |outward_x|
    // — a 90° rotation of outward, with the rotation direction
    // chosen so the resulting vector points into the mark's body.
    constexpr qreal k_wing_pen_w   = 1.2;
    constexpr qreal k_half_pen     = k_wing_pen_w / 2.0;
    // Extend the wing's tail INTO the bar by this much (measured along
    // the wing direction) so the bar's fill, painted afterward, has a
    // generous overlap to mask.
    constexpr qreal k_overlap_into_bar = 1.0;

    const qreal L          = std::sqrt(k_wing_len * k_wing_len
                                       + k_wing_dy * k_wing_dy);
    const qreal unit_dx    = k_wing_len / L;    // unit outward X magnitude
    const qreal unit_dy    = k_wing_dy  / L;    // unit outward Y magnitude

    QPen wing_pen(Qt::black, k_wing_pen_w);
    wing_pen.setCapStyle(Qt::FlatCap);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(wing_pen);

    // x_sign: which side of the bar the wing emerges from.  For
    //   begin (outward = right): +1.  For end (outward = left): -1.
    const qreal x_sign      = is_begin ? +1.0 : -1.0;
    const qreal bar_edge_x  = is_begin ? (thick_x + k_thick_w) : thick_x;

    // y_sign distinguishes top wing (rises up) from bottom wing
    // (falls down).
    auto paint_wing = [&](qreal corner_y, qreal y_sign) {
        const qreal ox     = x_sign * unit_dx;          // outward X (signed)
        const qreal oy     = -y_sign * unit_dy;         // outward Y (signed)
        const qreal perp_x = x_sign * unit_dy;          // perp X (signed)
        const qreal perp_y = y_sign * unit_dx;          // perp Y (signed)

        const qreal anchor_x = bar_edge_x
                              + perp_x * k_half_pen
                              - ox      * k_overlap_into_bar;
        const qreal anchor_y = corner_y
                              + perp_y * k_half_pen
                              - oy      * k_overlap_into_bar;

        const qreal tip_outer_x = bar_edge_x + ox * k_wing_len;
        const qreal tip_outer_y = corner_y   + oy * k_wing_len;
        const qreal tip_x       = tip_outer_x + perp_x * k_half_pen;
        const qreal tip_y       = tip_outer_y + perp_y * k_half_pen;

        QPainterPath wp;
        wp.moveTo(anchor_x, anchor_y);
        wp.lineTo(tip_x,    tip_y);
        painter.drawPath(wp);
    };

    paint_wing(top,    +1.0);   // top wing: rises (outward Y negative)
    paint_wing(bottom, -1.0);   // bottom wing: falls (outward Y positive)

    // --- Bars (painted AFTER wings to mask their inside portion) ---
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    painter.drawRect(QRectF(thick_x, top, k_thick_w, bottom - top));
    painter.drawRect(QRectF(thin_x,  top, k_thin_w,  bottom - top));

    // --- Dots ---
    // Vertically split around the mark's centre.  The dot column
    // sits between the two bars, biased toward the thin-bar side so
    // the dots aren't visually crowded against the thick bar.
    qreal centre_y = (top + bottom) / 2.0;
    qreal dot_x;
    if (is_begin)
        dot_x = thick_x + k_thick_w + k_bars_gap + k_thin_w + k_dot_inset;
    else
        dot_x = thick_x - k_bars_gap - k_thin_w - k_dot_inset;
    qreal dot_dy = std::max(4.0, (bottom - top) / 6.0);
    painter.drawEllipse(QPointF(dot_x, centre_y - dot_dy), k_dot_r, k_dot_r);
    painter.drawEllipse(QPointF(dot_x, centre_y + dot_dy), k_dot_r, k_dot_r);

    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: is_duration_mode
// ---------------------------------------------------------------------------
bool bar_renderer::is_duration_mode(const model::bar& bar)
{
    if (bar.empty())
        return false;
    // Model guarantees no mixed bars — check only the first chord.
    return bar.chords().front().duration().has_value();
}

} // namespace nashville::view
