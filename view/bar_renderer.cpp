#include "bar_renderer.hpp"
#include <QFontMetricsF>
#include <cmath>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// Public: width_hint
// ---------------------------------------------------------------------------
qreal bar_renderer::width_hint(const model::bar& bar,
                              qreal /*height*/,
                              const chord_renderer::Fonts& fonts)
{
    if (bar.empty())
        return 0.0;

    qreal total_width = 0.0;
    for (const auto& ch : bar.chords())
        total_width += chord_renderer::size_hint(ch, fonts).width();

    total_width += k_inter_chord_spacing * (bar.chords().size() - 1);
    total_width += k_time_sig_slot_w;
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
                         bool line_has_articulation)
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

    // --- Time signature: always reserve k_time_sig_slot_w so chord columns align ---
    // Paint glyphs only when this bar actually has a time sig change.
    qreal chords_left = rect.left() + k_time_sig_slot_w;

    if (bar.time_sig())
    {
        const auto& ts = *bar.time_sig();
        QFont tsFont = fonts.modifier;
        QFontMetricsF fm(tsFont);

        QString countStr = QString::number(ts.count());
        QString kindStr  = QString::number(static_cast<int>(ts.kind()));
        qreal max_adv = std::max(fm.horizontalAdvance(countStr),
                                fm.horizontalAdvance(kindStr));
        qreal ts_x    = rect.left() + (k_time_sig_slot_w - max_adv) / 2.0;  // centred in slot
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
                          rect.width() - k_time_sig_slot_w,
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
