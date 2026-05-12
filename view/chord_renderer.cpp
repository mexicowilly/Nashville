#include "chord_renderer.hpp"
#include <QFontMetricsF>
#include <QPainterPath>
#include <cmath>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// Unicode glyphs
// ---------------------------------------------------------------------------
static constexpr const char* kFlat     = "\u266D"; // ♭
static constexpr const char* kSharp    = "\u266F"; // ♯
static constexpr const char* kDiminish = "\u00B0"; // °

// ---------------------------------------------------------------------------
// Public: size_hint
// ---------------------------------------------------------------------------
QSizeF chord_renderer::size_hint(const model::chord& ch, const Fonts& fonts)
{
    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF modFm(fonts.modifier);

    qreal row_width = 0;

    if (ch.step())
        row_width += modFm.horizontalAdvance(kFlat) + k_element_spacing;

    row_width += nmFm.horizontalAdvance(QString::number(ch.number()));

    switch (ch.mode())
    {
        case model::chord::type::MINOR:
            row_width += modFm.horizontalAdvance("-") + k_element_spacing;
            break;
        case model::chord::type::DIMINISHED:
            row_width += modFm.horizontalAdvance(kDiminish) + k_element_spacing;
            break;
        case model::chord::type::AUGMENTED:
            row_width += modFm.horizontalAdvance("+") + k_element_spacing;
            break;
        default: break;
    }

    if (!ch.extensions().empty())
        row_width += modFm.horizontalAdvance(
                        QString::fromStdString(ch.extensions())) + k_element_spacing;

    if (ch.bass_note())
    {
        row_width += modFm.horizontalAdvance("/") + k_element_spacing;
        if (ch.bass_note_step())
            row_width += modFm.horizontalAdvance(kFlat) + k_element_spacing;
        row_width += modFm.horizontalAdvance(QString::number(*ch.bass_note()));
    }

    qreal number_height = nmFm.height();
    qreal total_height  = number_height / k_number_zone_ratio;

    return QSizeF(row_width, total_height);
}

// ---------------------------------------------------------------------------
// Public: paint
// ---------------------------------------------------------------------------
void chord_renderer::paint(QPainter& painter,
                          const QRectF& rect,
                          const model::chord& ch,
                          const Fonts& fonts,
                          bool is_duration_mode,
                          bool line_has_articulation)
{
    if (ch.mode() == model::chord::type::UNDEFINED)
        return;

    painter.save();

    QFontMetricsF art_fm(fonts.articulation);
    qreal art_height = line_has_articulation
                       ? art_fm.ascent() + art_fm.descent()
                       : 0.0;
    constexpr qreal k_plain_top_pad = 4.0;
    qreal top_offset = art_height > 0.0 ? art_height : k_plain_top_pad;
    QRectF artRect(rect.left(), rect.top(), rect.width(), art_height);
    QRectF numRect(rect.left(), rect.top() + top_offset,
                   rect.width(), rect.height() - top_offset);

    // Paint number row; get tight rect around the number glyph for diamond.
    qreal row_right = 0.0;
    QRectF numberGlyphRect = paint_number_row(painter, numRect, ch, fonts, row_right);

    if (line_has_articulation)
        paint_articulations(painter, artRect, ch, fonts,
                            numberGlyphRect.center().x(), row_right);

    if (ch.is_diamond())
        paint_diamond(painter, numberGlyphRect, artRect.bottom(), numRect.bottom() - 1.0);

    painter.restore();
}

// ---------------------------------------------------------------------------
// Public: paintRhythm
// ---------------------------------------------------------------------------
void chord_renderer::paint_rhythm(QPainter& painter,
                                  const QRectF& rect,
                                  const model::chord& ch,
                                  const Fonts& fonts)
{
    if (!ch.duration())
        return;

    const bool dotted  = is_dotted(*ch.duration());
    const bool is_half = (*ch.duration() == model::chord::time::HALF ||
                          *ch.duration() == model::chord::time::DOTTED_HALF);
    const bool is_whole = (*ch.duration() == model::chord::time::WHOLE);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor ink = painter.pen().color();
    constexpr qreal k_stem_gap = 3.0;

    // All glyphs drawn from Bravura. Use the notehead glyph to size the font,
    // then draw the stem and flags as primitives/separate glyphs.
    // noteheadBlack = U+E0A4, noteheadHalf = U+E0A3, noteheadWhole = U+E0A2
    QString head_glyph = is_whole ? "\uE0A2" : (is_half ? "\uE0A3" : "\uE0A4");

    QFont glyphFont = fonts.music;

    // Scale so the notehead occupies ~65% of the row height.
    {
        const qreal px_per_pt = 96.0 / 72.0;
        const qreal target_px = rect.height() * 0.65;
        qreal pt_size = target_px / (0.3 * px_per_pt);
        glyphFont.setPointSizeF(pt_size);
        QFontMetricsF fm0(glyphFont);
        qreal ascent_px = fm0.ascent();
        if (ascent_px > 0.0)
        {
            qreal head_px = ascent_px * 0.28;
            if (head_px > 0.0)
                glyphFont.setPointSizeF(pt_size * (target_px / head_px));
        }
    }

    painter.setFont(glyphFont);
    QFontMetricsF fm(glyphFont);

    qreal notehead_h = fm.ascent() * 0.28;

    // Clip k_stem_gap below rect.top() so stems stop short of the rule.
    painter.setClipRect(rect.adjusted(0, k_stem_gap, 0, 0));

    // Baseline: place so the notehead bottom is near rect.bottom().
    qreal baseline = rect.bottom() - notehead_h * 0.25;

    // Compensate for any negative left bearing in the Bravura glyph.
    QRectF head_tbr = fm.tightBoundingRect(head_glyph);
    qreal draw_x = rect.left() - std::min(0.0, head_tbr.left());

    painter.setPen(QPen(ink, 1.0));
    painter.drawText(QPointF(draw_x, baseline), head_glyph);

    qreal head_top_y    = baseline + head_tbr.top();
    qreal head_bottom_y = baseline + head_tbr.bottom();
    qreal head_right_x  = draw_x + head_tbr.right();

    // Stem: all notes except whole get a manual upward stem from the right edge.
    qreal stem_x  = head_right_x - 1.0;
    qreal stem_y1 = rect.top() + k_stem_gap;   // top of stem (near rule)
    if (!is_whole)
    {
        qreal stem_y0 = head_top_y + 1.0;
        painter.setPen(QPen(ink, 1.0));
        painter.drawLine(QPointF(stem_x, stem_y0), QPointF(stem_x, stem_y1));
    }

    // Flag glyph drawn at the top of the stem, scaled to fit within the stem length.
    // flag8thUp = U+E240, flag16thUp = U+E242.
    QString flag_glyph;
    switch (*ch.duration())
    {
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:  flag_glyph = "\uE240"; break;
        case model::chord::time::SIXTEENTH:      flag_glyph = "\uE242"; break;
        default: break;
    }
    if (!flag_glyph.isEmpty())
    {
        // Scale the flag so its height fits the stem length.
        qreal available_h = head_top_y - stem_y1;
        QFont flagFont = glyphFont;
        {
            QFontMetricsF fm0(flagFont);
            qreal flag_h = fm0.tightBoundingRect(flag_glyph).height();
            if (flag_h > 0.0 && available_h > 0.0)
                flagFont.setPointSizeF(flagFont.pointSizeF() * (available_h / flag_h));
        }
        painter.setFont(flagFont);
        painter.setPen(QPen(ink, 1.0));
        QFontMetricsF flag_fm(flagFont);
        QRectF flag_tbr = flag_fm.tightBoundingRect(flag_glyph);
        // Draw so the top of the flag sits at stem_y1.
        painter.drawText(QPointF(stem_x, stem_y1 - flag_tbr.top()), flag_glyph);
        painter.setFont(glyphFont);  // restore for dot advance-width calc
    }

    // Augmentation dot: right of notehead, centred vertically on the notehead.
    if (dotted)
    {
        qreal dot_r = rect.height() * 0.06;
        qreal dot_x = draw_x + fm.horizontalAdvance(head_glyph) + k_element_spacing + dot_r;
        qreal dot_y = (head_top_y + head_bottom_y) / 2.0;  // centre of notehead for all types
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(dot_x, dot_y), dot_r, dot_r);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Public: paint_articulations
// Shared articulation dispatcher used by both paint() (plain mode) and
// bar_renderer (duration mode, where artRect lives above the chord slot).
// ---------------------------------------------------------------------------
void chord_renderer::paint_articulations(QPainter& painter,
                                         const QRectF& artRect,
                                         const model::chord& ch,
                                         const Fonts& fonts,
                                         qreal number_center_x,
                                         qreal row_right)
{
    if (ch.is_staccato() && ch.is_pushed())
    {
        qreal mid = artRect.top() + artRect.height() / 2.0;
        QRectF staccatoRect(artRect.left(), artRect.top(), artRect.width(), artRect.height() / 2.0);
        QRectF pushedRect(artRect.left(), mid, artRect.width(), artRect.height() / 2.0);
        paint_staccato(painter, staccatoRect, number_center_x);
        paint_pushed(painter, pushedRect, fonts, number_center_x, artRect.top());
    }
    else
    {
        if (ch.is_staccato())
            paint_staccato(painter, artRect, number_center_x);
        if (ch.is_pushed())
            paint_pushed(painter, artRect, fonts, number_center_x, artRect.top());
    }

    if (ch.is_tied())
    {
        // The tie arc starts past the right edge of the full chord row
        // (number + mode + extensions + bass note).  This is correct for
        // both plain and diamond chords: row_right is always past the
        // diamond's right edge since the diamond surrounds only the
        // number glyph, which sits at the left of the row.
        paint_tied_arc(painter, artRect, /*diamond_left=*/-1.0, /*diamond_right=*/row_right);
    }
}

// ---------------------------------------------------------------------------
// Private: paint_number_row
// ---------------------------------------------------------------------------
QRectF chord_renderer::paint_number_row(QPainter& painter,
                                      const QRectF& rowRect,
                                      const model::chord& ch,
                                      const Fonts& fonts,
                                      qreal& row_right)
{
    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF modFm(fonts.modifier);

    // Compute total row width for centering
    qreal total_width = 0;
    if (ch.step())
        total_width += modFm.horizontalAdvance(kFlat) + k_element_spacing;
    total_width += nmFm.horizontalAdvance(QString::number(ch.number()));
    if (ch.mode() == model::chord::type::MINOR)
        total_width += modFm.horizontalAdvance("-") + k_element_spacing;
    else if (ch.mode() == model::chord::type::DIMINISHED)
        total_width += modFm.horizontalAdvance(kDiminish) + k_element_spacing;
    else if (ch.mode() == model::chord::type::AUGMENTED)
        total_width += modFm.horizontalAdvance("+") + k_element_spacing;
    if (!ch.extensions().empty())
        total_width += modFm.horizontalAdvance(
                          QString::fromStdString(ch.extensions())) + k_element_spacing;
    if (ch.bass_note())
    {
        total_width += modFm.horizontalAdvance("/") + k_element_spacing;
        if (ch.bass_note_step())
            total_width += modFm.horizontalAdvance(kFlat) + k_element_spacing;
        total_width += modFm.horizontalAdvance(QString::number(*ch.bass_note()));
    }

    // Compute baseline so the tight ink bounds of the number are centred in rowRect.
    QString num_str = QString::number(ch.number());
    QRectF tbr = nmFm.tightBoundingRect(num_str);
    // tbr.top() is negative (above baseline), tbr.bottom() is positive (below).
    // Centre: rowRect.center().y() == baseline + (tbr.top() + tbr.bottom()) / 2
    qreal baseline = rowRect.center().y() - (tbr.top() + tbr.bottom()) / 2.0;
    qreal x = rowRect.left();

    painter.save();

    // Step — raised slightly, smaller font
    x += paint_step(painter, ch, fonts, x, baseline - nmFm.ascent() * 0.4);

    // Number — dominant
    painter.setFont(fonts.number);
    qreal num_left  = x;
    painter.drawText(QPointF(x, baseline), num_str);

    // Tight ink rect for diamond sizing and centring.
    QRectF numberGlyphRect(num_left + tbr.left(),
                           baseline + tbr.top(),
                           tbr.width(),
                           tbr.height());
    x += nmFm.horizontalAdvance(num_str);

    // Mode suffix
    x += paint_mode(painter, ch, fonts, x, baseline);

    // Extensions — superscripted
    x += paint_extensions(painter, ch, fonts, x, baseline - nmFm.ascent() * 0.3);

    // Bass note
    x += paint_bass_note(painter, ch, fonts, x, baseline);

    painter.restore();

    row_right = x;
    return numberGlyphRect;
}

// ---------------------------------------------------------------------------
// Private: paint_step
// ---------------------------------------------------------------------------
qreal chord_renderer::paint_step(QPainter& painter,
                                const model::chord& ch,
                                const Fonts& fonts,
                                qreal x, qreal baseline)
{
    if (!ch.step())
        return 0.0;

    painter.setFont(fonts.modifier);
    QFontMetricsF fm(fonts.modifier);
    QString glyph = (ch.step() == model::chord::flat_sharp::FLAT)
                    ? QString(kFlat) : QString(kSharp);
    painter.drawText(QPointF(x, baseline), glyph);
    return fm.horizontalAdvance(glyph) + k_element_spacing;
}

// ---------------------------------------------------------------------------
// Private: paint_mode
// ---------------------------------------------------------------------------
qreal chord_renderer::paint_mode(QPainter& painter,
                                const model::chord& ch,
                                const Fonts& fonts,
                                qreal x, qreal baseline)
{
    painter.setFont(fonts.modifier);
    QFontMetricsF fm(fonts.modifier);
    QString suffix;

    switch (ch.mode())
    {
        case model::chord::type::MINOR:      suffix = "-";       break;
        case model::chord::type::DIMINISHED: suffix = kDiminish; break;
        case model::chord::type::AUGMENTED:  suffix = "+";       break;
        default: return 0.0;
    }

    // Align suffix to top of the number glyph rather than its baseline,
    // so -, °, + sit beside the upper portion of the chord number.
    // Caller passes the number baseline; compute number ascent from fonts.number.
    QFontMetricsF nmFm(fonts.number);
    qreal raised_baseline = baseline - nmFm.ascent() + fm.ascent();
    painter.drawText(QPointF(x, raised_baseline), suffix);
    return fm.horizontalAdvance(suffix) + k_element_spacing;
}

// ---------------------------------------------------------------------------
// Private: paint_extensions
// ---------------------------------------------------------------------------
qreal chord_renderer::paint_extensions(QPainter& painter,
                                      const model::chord& ch,
                                      const Fonts& fonts,
                                      qreal x, qreal baseline)
{
    if (ch.extensions().empty())
        return 0.0;

    painter.setFont(fonts.modifier);
    QFontMetricsF fm(fonts.modifier);
    QString ext = QString::fromStdString(ch.extensions());
    painter.drawText(QPointF(x, baseline), ext);
    return fm.horizontalAdvance(ext) + k_element_spacing;
}

// ---------------------------------------------------------------------------
// Private: paint_bass_note
// ---------------------------------------------------------------------------
qreal chord_renderer::paint_bass_note(QPainter& painter,
                                    const model::chord& ch,
                                    const Fonts& fonts,
                                    qreal x, qreal baseline)
{
    if (!ch.bass_note())
        return 0.0;

    painter.setFont(fonts.modifier);
    QFontMetricsF fm(fonts.modifier);
    qreal consumed = 0;

    painter.drawText(QPointF(x, baseline), "/");
    consumed += fm.horizontalAdvance("/") + k_element_spacing;
    x += consumed;

    if (ch.bass_note_step())
    {
        QString g = (ch.bass_note_step() == model::chord::flat_sharp::FLAT)
                    ? QString(kFlat) : QString(kSharp);
        painter.drawText(QPointF(x, baseline), g);
        qreal w = fm.horizontalAdvance(g) + k_element_spacing;
        consumed += w;
        x += w;
    }

    QString bn = QString::number(*ch.bass_note());
    painter.drawText(QPointF(x, baseline), bn);
    consumed += fm.horizontalAdvance(bn);

    return consumed;
}

// ---------------------------------------------------------------------------
// Private: paint_staccato — filled dot above everything (larger, more visible)
// ---------------------------------------------------------------------------
void chord_renderer::paint_staccato(QPainter& painter, const QRectF& artRect,
                                    qreal number_center_x)
{
    painter.save();
    constexpr qreal dot_r = 3.0;
    qreal cx    = number_center_x;
    qreal cy    = artRect.bottom() - dot_r - 1.0;
    QPolygonF diamond;
    diamond << QPointF(cx,          cy - dot_r)
            << QPointF(cx + dot_r,  cy)
            << QPointF(cx,          cy + dot_r)
            << QPointF(cx - dot_r,  cy);
    painter.setBrush(painter.pen().color());
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(diamond);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_pushed — '>' below staccato dot
// ---------------------------------------------------------------------------
void chord_renderer::paint_pushed(QPainter& painter,
                                 const QRectF& artRect,
                                 const Fonts& fonts,
                                 qreal number_center_x,
                                 qreal /*number_top_y*/)
{
    painter.save();
    painter.setFont(fonts.articulation);
    QFontMetricsF fm(fonts.articulation);
    // Centre the > vertically within the articulation zone.
    qreal baseline = artRect.bottom();
    qreal x = number_center_x - fm.horizontalAdvance(">") / 2.0;
    painter.drawText(QPointF(x, baseline), ">");
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_tied_arc — smooth cubic-bezier arc, belly curves upward
// Endpoints sit at the bottom of the tie zone; arc bulges upward toward arc_top.
// ---------------------------------------------------------------------------
void chord_renderer::paint_tied_arc(QPainter& painter, const QRectF& artRect,
                                    qreal diamond_left, qreal diamond_right)
{
    painter.save();

    // arc_top is the peak of the upward bulge; base_y is the endpoint height.
    qreal arc_top  = artRect.top() + artRect.height() * k_tied_top_ratio;
    qreal arc_span = artRect.height() * 0.40;   // deeper arc
    qreal margin   = artRect.width() * 0.08;

    qreal base_y   = arc_top + arc_span;

    // When a diamond is present the tie arc sits entirely to its right.
    // Otherwise span the full slot width with a small margin.
    // The tie extends to the right edge of the slot (no right margin) so it
    // visually connects to the next bar.  When a diamond is present the left
    // endpoint starts just to its right; otherwise it starts near the left edge.
    qreal start_x = diamond_right + margin;
    qreal end_x   = artRect.right() + artRect.width() * 0.18;  // overhang into inter-bar gap

    // Symmetric cubic bezier: endpoints at base_y, control points pushed
    // above arc_top so the actual curve peak reaches close to arc_top.
    qreal cp_y = arc_top - arc_span * 0.15;   // slightly above arc_top
    QPainterPath path;
    path.moveTo(start_x, base_y);
    path.cubicTo(start_x + (end_x - start_x) * 0.25, cp_y,
                 start_x + (end_x - start_x) * 0.75, cp_y,
                 end_x, base_y);

    painter.setPen(QPen(painter.pen().color(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_diamond — drawn around the number glyph rect
// ---------------------------------------------------------------------------
void chord_renderer::paint_diamond(QPainter& painter, const QRectF& numberRect,
                                   qreal /*art_bottom*/, qreal /*max_bottom*/)
{
    painter.save();
    QRectF r = numberRect.adjusted(-k_diamond_padding_h, -k_diamond_padding_v,
                                    k_diamond_padding_h,  k_diamond_padding_v);
    QPointF center = r.center();
    QPolygonF diamond;
    diamond << QPointF(center.x(), r.top())
            << QPointF(r.right(),  center.y())
            << QPointF(center.x(), r.bottom())
            << QPointF(r.left(),   center.y());

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(painter.pen().color(), 1.5));
    painter.drawPolygon(diamond);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: rhythm helpers
// ---------------------------------------------------------------------------
bool chord_renderer::is_dotted(model::chord::time duration)
{
    return duration == model::chord::time::DOTTED_EIGHTH  ||
           duration == model::chord::time::DOTTED_QUARTER ||
           duration == model::chord::time::DOTTED_HALF;
}

} // namespace nashville::view
