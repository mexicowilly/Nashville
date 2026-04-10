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
static constexpr const char* kQuarter  = "\u2669"; // ♩
static constexpr const char* kEighth   = "\u266A"; // ♪
static constexpr const char* kBeamed   = "\u266C"; // ♬ (sixteenth fallback)

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
                          bool /*is_duration_mode*/)
{
    if (ch.mode() == model::chord::type::UNDEFINED)
        return;

    qreal art_height = rect.height() * k_articulation_zone_ratio;
    QRectF artRect(rect.left(), rect.top(), rect.width(), art_height);
    QRectF numRect(rect.left(), rect.top() + art_height,
                   rect.width(), rect.height() - art_height);

    // Paint number row; get tight rect around the number glyph for diamond
    QRectF numberGlyphRect = paint_number_row(painter, numRect, ch, fonts);

    // Articulations — order: staccato (top), pushed, tied, diamond (around number)
    if (ch.is_staccato())
        paint_staccato(painter, artRect, numberGlyphRect.center().x());

    if (ch.is_pushed())
        paint_pushed(painter, artRect, fonts);

    if (ch.is_tied())
    {
        // Pass diamond horizontal bounds so arc endpoints clear the diamond.
        qreal diamond_left  = -1.0;
        qreal diamond_right = -1.0;
        if (ch.is_diamond())
        {
            QRectF dr = numberGlyphRect.adjusted(
                -k_diamond_padding_h, -k_diamond_padding_v,
                 k_diamond_padding_h,  k_diamond_padding_v);
            diamond_left  = dr.left();
            diamond_right = dr.right();
        }
        paint_tied_arc(painter, artRect, diamond_left, diamond_right);
    }

    if (ch.is_diamond())
        paint_diamond(painter, numberGlyphRect);
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

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor ink = painter.pen().color();

    // Convert rect.height() to points so the glyph fills the row.
    // Bravura's em is very large, so we use a fraction of the pixel height
    // converted to points (assuming 96 dpi: 1pt = 96/72 px).
    // Target: notehead occupies ~65% of row height.
    const qreal px_per_pt = 96.0 / 72.0;
    const qreal target_px = rect.height() * 0.65;
    // For Bravura, the notehead is approximately 0.3 × the em height in pixels.
    // So pt = target_px / (0.3 * px_per_pt).  We refine with one measurement.
    qreal pt_size = target_px / (0.3 * px_per_pt);

    QFont scaledFont = fonts.music;
    scaledFont.setPointSizeF(pt_size);

    // One refinement: measure the actual quarter-note advance width (which
    // scales linearly with em) and adjust so the notehead fits the row.
    {
        QFontMetricsF fm0(scaledFont);
        // ascent() is proportional to em and reliable across fonts.
        qreal ascent_px = fm0.ascent();
        if (ascent_px > 0.0)
        {
            // Empirically, Bravura notehead height ≈ 0.28 × ascent.
            qreal head_px = ascent_px * 0.28;
            if (head_px > 0.0)
                scaledFont.setPointSizeF(pt_size * (target_px / head_px));
        }
    }

    painter.setFont(scaledFont);
    QFontMetricsF fm(scaledFont);

    qreal notehead_h = fm.ascent() * 0.28;   // estimated notehead height

    // Gap between stem tip and the rule above rect.top().
    constexpr qreal k_stem_gap = 3.0;

    // Clip k_stem_gap below rect.top() so stems stop short of the rule.
    painter.setClipRect(rect.adjusted(0, k_stem_gap, 0, 0));

    // Baseline: place so the notehead bottom is near rect.bottom().
    // For Bravura glyphs the notehead sits at approximately 0.3 × ascent
    // above the baseline, so nudge baseline up by that amount.
    qreal baseline = rect.bottom() - notehead_h * 0.25;

    QString glyph = note_glyph(*ch.duration());
    painter.setPen(QPen(ink, 1.0));
    painter.drawText(QPointF(rect.left(), baseline), glyph);

    // Use the tight bounding rect to find the actual rendered glyph extents.
    // tbr.top() and tbr.bottom() are relative to the baseline (top is negative = above).
    QRectF tbr = fm.tightBoundingRect(glyph);
    qreal glyph_top_y    = baseline + tbr.top();     // topmost rendered pixel
    qreal glyph_bottom_y = baseline + tbr.bottom();  // bottommost rendered pixel

    // Half note: stem from right edge of notehead upward.
    // U+E0A3 is a bare notehead, so the bounding box IS the notehead.
    // Start the stem at the top of the bounding box so it connects flush.
    if (is_half)
    {
        qreal stem_x  = rect.left() + tbr.right() - 1.0;
        qreal stem_y0 = glyph_top_y + 1.0;        // sit on top edge of notehead
        qreal stem_y1 = rect.top() + k_stem_gap;
        painter.setPen(QPen(ink, 1.0));
        painter.drawLine(QPointF(stem_x, stem_y0), QPointF(stem_x, stem_y1));
    }

    // Augmentation dot: right of glyph, centred on the notehead.
    // For U+E0A3 (bare notehead) the bbox IS the notehead: centre = mid of bbox.
    // For ♩/♪ glyphs the bbox spans notehead + stem; notehead sits at the bottom,
    // so its centre = glyph_bottom_y - notehead_h / 2.
    if (dotted)
    {
        qreal dot_r = notehead_h * 0.10;
        qreal dot_x = rect.left() + fm.horizontalAdvance(glyph)
                      + k_element_spacing + dot_r;
        qreal dot_y = is_half
                      ? (glyph_top_y + glyph_bottom_y) / 2.0          // bbox IS the notehead
                      : glyph_bottom_y - notehead_h / 2.0 + 2.0;       // notehead centre, nudged down
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(dot_x, dot_y), dot_r, dot_r);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_number_row
// ---------------------------------------------------------------------------
QRectF chord_renderer::paint_number_row(QPainter& painter,
                                      const QRectF& rowRect,
                                      const model::chord& ch,
                                      const Fonts& fonts)
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

    qreal baseline = rowRect.top()
                     + (rowRect.height() + nmFm.ascent() - nmFm.descent()) / 2.0;
    qreal x = rowRect.left();

    painter.save();

    // Step — raised slightly, smaller font
    x += paint_step(painter, ch, fonts, x, baseline - nmFm.ascent() * 0.4);

    // Number — dominant
    painter.setFont(fonts.number);
    QString num_str   = QString::number(ch.number());
    qreal num_left    = x;
    painter.drawText(QPointF(x, baseline), num_str);
    QRectF numberGlyphRect(num_left,
                           baseline - nmFm.ascent(),
                           nmFm.horizontalAdvance(num_str),
                           nmFm.ascent() + nmFm.descent());
    x += nmFm.horizontalAdvance(num_str);

    // Mode suffix
    x += paint_mode(painter, ch, fonts, x, baseline);

    // Extensions — superscripted
    x += paint_extensions(painter, ch, fonts, x, baseline - nmFm.ascent() * 0.3);

    // Bass note
    paint_bass_note(painter, ch, fonts, x, baseline);

    painter.restore();

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
    qreal dot_r = artRect.height() * 0.18;   // larger dot, more visible
    qreal cx    = number_center_x;            // centered over the chord number
    qreal cy    = artRect.top() + artRect.height() * k_staccato_top_ratio + dot_r;
    painter.setBrush(painter.pen().color());
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(cx, cy), dot_r, dot_r);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_pushed — '>' below staccato dot
// ---------------------------------------------------------------------------
void chord_renderer::paint_pushed(QPainter& painter,
                                 const QRectF& artRect,
                                 const Fonts& fonts)
{
    painter.save();
    painter.setFont(fonts.articulation);
    QFontMetricsF fm(fonts.articulation);
    qreal y = artRect.top() + artRect.height() * k_pushed_top_ratio + fm.ascent();
    qreal x = artRect.center().x() - fm.horizontalAdvance(">") / 2.0;
    painter.drawText(QPointF(x, y), ">");
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
    qreal start_x = (diamond_right >= 0.0) ? diamond_right + margin
                                            : artRect.left() + margin;
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
void chord_renderer::paint_diamond(QPainter& painter, const QRectF& numberRect)
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
QString chord_renderer::note_glyph(model::chord::time duration)
{
    switch (duration)
    {
        case model::chord::time::QUARTER:
        case model::chord::time::DOTTED_QUARTER:  return "\u2669";  // ♩
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:   return "\u266A";  // ♪
        case model::chord::time::SIXTEENTH:       return "\u266C";  // ♬
        case model::chord::time::HALF:
        case model::chord::time::DOTTED_HALF:     return "\uE0A3";  // Bravura: noteheadHalf (stem drawn manually)
        case model::chord::time::WHOLE:           return "\uE0A2";  // Bravura: noteheadWhole
        default:                                  return "?";
    }
}

bool chord_renderer::is_dotted(model::chord::time duration)
{
    return duration == model::chord::time::DOTTED_EIGHTH  ||
           duration == model::chord::time::DOTTED_QUARTER ||
           duration == model::chord::time::DOTTED_HALF;
}

} // namespace nashville::view
