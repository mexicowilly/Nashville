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
        paint_staccato(painter, artRect);

    if (ch.is_pushed())
        paint_pushed(painter, artRect, fonts);

    if (ch.is_tied())
        paint_tied_arc(painter, artRect);

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

    painter.save();
    painter.setFont(fonts.music);
    QFontMetricsF fm(fonts.music);

    QString glyph = note_glyph(*ch.duration());
    bool dotted   = is_dotted(*ch.duration());

    qreal total_width = fm.horizontalAdvance(glyph);
    if (dotted)
        total_width += k_element_spacing + fm.ascent() * 0.15;

    qreal x        = rect.left();
    qreal baseline = rect.top() + (rect.height() + fm.ascent() - fm.descent()) / 2.0;

    painter.drawText(QPointF(x, baseline), glyph);
    x += fm.horizontalAdvance(glyph);

    if (dotted)
    {
        // Smaller, proportional dot for augmentation dot
        qreal dot_r = fm.ascent() * 0.12;
        qreal dot_x = x + k_element_spacing + dot_r;
        qreal dot_y = baseline - fm.ascent() * 0.4;
        painter.setBrush(painter.pen().color());
        painter.setPen(Qt::NoPen);
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
void chord_renderer::paint_staccato(QPainter& painter, const QRectF& artRect)
{
    painter.save();
    // Larger staccato dot for better visibility
    qreal dot_r = artRect.height() * 0.12;
    qreal cx    = artRect.center().x();
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
// Private: paint_tied_arc — smooth cubic-bezier arc below pushed
// Uses cubic bezier for a smoother, more even curve like MuseScore
// ---------------------------------------------------------------------------
void chord_renderer::paint_tied_arc(QPainter& painter, const QRectF& artRect)
{
    painter.save();

    // Arc geometry
    qreal arc_top  = artRect.top() + artRect.height() * k_tied_top_ratio;
    qreal arc_span = artRect.height() * 0.22;
    qreal margin   = artRect.width() * 0.12;

    qreal start_x = artRect.left() + margin;
    qreal end_x   = artRect.right() - margin;
    qreal mid_x   = artRect.center().x();
    qreal base_y  = arc_top + arc_span;

    // Cubic bezier for smoother, more natural arc
    // Control points lift the curve up in the middle for an even shape
    QPainterPath path;
    path.moveTo(start_x, base_y);
    path.cubicTo(start_x + (mid_x - start_x) * 0.5, arc_top + arc_span * 0.2,
                 mid_x - (mid_x - start_x) * 0.5, arc_top,
                 mid_x, arc_top);
    path.cubicTo(mid_x + (end_x - mid_x) * 0.5, arc_top,
                 mid_x + (end_x - mid_x) * 0.5, arc_top + arc_span * 0.2,
                 end_x, base_y);

    // Thin, smooth line
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
    QRectF r = numberRect.adjusted(-k_diamond_padding, -k_diamond_padding,
                                    k_diamond_padding,  k_diamond_padding);
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
        case model::chord::time::DOTTED_QUARTER:
            return QString(kQuarter);
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:
            return QString(kEighth);
        case model::chord::time::SIXTEENTH:
            return QString(kBeamed);
        case model::chord::time::HALF:
        case model::chord::time::DOTTED_HALF:
            return "\uE0A3";  // Bravura PUA: noteheadHalf
        case model::chord::time::WHOLE:
            return "\uE0A2";  // Bravura PUA: noteheadWhole
        default:
            return QString("?");
    }
}

bool chord_renderer::is_dotted(model::chord::time duration)
{
    return duration == model::chord::time::DOTTED_EIGHTH  ||
           duration == model::chord::time::DOTTED_QUARTER ||
           duration == model::chord::time::DOTTED_HALF;
}

} // namespace nashville::view
