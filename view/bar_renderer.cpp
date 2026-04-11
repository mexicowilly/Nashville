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

    qreal top_pad      = rect.height() * 0.05;
    qreal chord_slot_h  = rect.height() * k_chord_slot_ratio;
    qreal rhythm_row_h = rect.height() * k_rhythm_row_ratio;

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
                          rect.top() + top_pad,
                          rect.width() - k_time_sig_slot_w,
                          chord_slot_h);

    // Compute line_y to sit just below the rendered number glyph.
    // paint_number_row centres the number vertically in the number zone, so
    // the actual glyph bottom = num_zone_top + (num_zone_h + ascent + descent) / 2.
    // Using this exact value keeps the rule flush below the number in both
    // plain bars and duration-mode bars without overshooting.
    qreal line_y = 0.0;
    {
        QFontMetricsF nmFm(fonts.number);
        const qreal num_zone_top = chord_slot_rect.top()
                                   + chord_slot_h * chord_renderer::k_articulation_zone_ratio;
        const qreal num_zone_h   = chord_slot_h * chord_renderer::k_number_zone_ratio;
        const qreal glyph_bottom = num_zone_top
                                   + (num_zone_h + nmFm.ascent() + nmFm.descent()) / 2.0;
        line_y = std::floor(glyph_bottom) + 1.0;
    }

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
