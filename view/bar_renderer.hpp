#pragma once

#include "../model/bar.hpp"
#include "chord_renderer.hpp"
#include <QPainter>
#include <QRectF>

namespace nashville::view
{

// Stateless renderer for a single bar.
class bar_renderer
{
public:
    // Returns the minimum width needed to render this bar.  The repeat-
    // mark flags grow the width when set so the painted glyphs have
    // dedicated horizontal space at the bar's left/right edges and don't
    // squeeze the chord row.
    static qreal width_hint(const model::bar& bar,
                           qreal height,
                           const chord_renderer::Fonts& fonts,
                           bool draw_begin_repeat = false,
                           bool draw_end_repeat   = false);

    // Paints the bar into rect.
    // line_duration_mode: true if any bar on this line is duration-mode,
    // which controls whether the rule and rhythm row zones are allocated.
    // draw_begin_repeat / draw_end_repeat: paint a repeat mark at the
    // left / right edge of the bar.  The renderer reserves matching slot
    // widths internally (mirroring width_hint), so the chord row stays
    // visually balanced regardless of which side carries a mark.
    static void paint(QPainter& painter,
                      const QRectF& rect,
                      const model::bar& bar,
                      const chord_renderer::Fonts& fonts,
                      bool line_duration_mode,
                      bool line_has_articulation,
                      bool draw_begin_repeat = false,
                      bool draw_end_repeat   = false,
                      bool draw_beat_parens  = false);

    // Returns the y-coordinate at the vertical centre of the number row
    // for a bar laid out into `rect`.  Match this value when placing any
    // adornment that should sit on the chord-number line (e.g. the
    // continuation dot painted between bars when a line exceeds
    // bars_per_line()).  The computation mirrors chord_renderer's
    // numRect.center().y() exactly so the adornment aligns pixel-for-
    // pixel with the chord glyphs.
    static qreal number_row_center_y(const QRectF& rect,
                                     const model::bar& bar,
                                     const chord_renderer::Fonts& fonts,
                                     bool line_duration_mode,
                                     bool line_has_articulation);

    // Width of a repeat-mark slot at the left or right edge of a bar.
    // Public so the layout pass can include it in width budgeting and
    // volta-bracket placement.  Sized to give the wings room to extend
    // outward without colliding with the bar's chord column.
    static constexpr qreal k_repeat_slot_w = 12.0;

    // Width always reserved at the left of a bar for a time-signature glyph.
    // Bars without a time sig leave this space empty so chord columns align.
    // Public so paint_beat_dots in song_body_widget can align the dot row
    // with the chord column.
    static constexpr qreal k_time_sig_slot_w = 20.0;

    // Vertical split ratios for duration-mode bars (public so callers can derive heights)
    static constexpr qreal k_chord_slot_ratio = 0.60;
    static constexpr qreal k_rule_thickness   = 1.0;
    static constexpr qreal k_rhythm_row_ratio = 0.30;

    // True if this bar should render the rhythm row — the rule plus the
    // duration/rest symbols beneath it.  A bar qualifies when any of its
    // chords carries a duration OR is a rest: rests are inherently rhythmic
    // and always live below the rule (an unset duration renders as a whole
    // rest), so a bar holding one is in duration mode even with no explicit
    // durations.  Public so the layout pass in song_body_widget shares this
    // single definition rather than re-deriving it.
    static bool is_duration_mode(const model::bar& bar);

private:
    // Paints a single repeat mark.  `mark_rect` is the slot the glyph
    // should occupy (width == k_repeat_slot_w); `is_begin` selects which
    // side the dots and wings face.
    //   begin: thick-then-thin bars, dots on the right, wings hooking
    //          to the right of the thick bar (towards the music being
    //          repeated).
    //   end:   thin-then-thick bars, dots on the left, wings hooking
    //          to the left.
    // The "wings" are short angled strokes from the top and bottom of
    // the thick bar; Nashville charts use them because there is no
    // staff to anchor the dots' vertical position, and the wings
    // visually compensate.
    static void paint_repeat_mark(QPainter& painter,
                                  const QRectF& mark_rect,
                                  bool is_begin);

    // Width given to the (otherwise zero-width) chord row of an empty
    // bar — i.e. one with no chords yet, as produced by the "Insert 1
    // before/after" menu actions.  Sized to roughly one chord-glyph
    // worth of horizontal space so the new bar reads as a recognisable
    // column in the layout and is comfortably click-able once the user
    // decides to edit it.  A genuinely zero-width slot would only show
    // through k_bar_padding as a thin gap, which isn't enough visual
    // signal that a bar landed there.
    static constexpr qreal k_empty_bar_chord_slot_w = 20.0;

    // Horizontal gap between chords within a bar
    static constexpr qreal k_inter_chord_spacing = 6.0;
};

} // namespace nashville::view
