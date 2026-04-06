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
// Public: sizeHint
// ---------------------------------------------------------------------------
QSizeF ChordRenderer::sizeHint(const model::chord& ch, const Fonts& fonts)
{
    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF modFm(fonts.modifier);

    qreal rowWidth = 0;

    if (ch.step())
        rowWidth += modFm.horizontalAdvance(kFlat) + kElementSpacing;

    rowWidth += nmFm.horizontalAdvance(QString::number(ch.number()));

    switch (ch.mode())
    {
        case model::chord::type::MINOR:
            rowWidth += modFm.horizontalAdvance("-") + kElementSpacing;
            break;
        case model::chord::type::DIMINISHED:
            rowWidth += modFm.horizontalAdvance(kDiminish) + kElementSpacing;
            break;
        case model::chord::type::AUGMENTED:
            rowWidth += modFm.horizontalAdvance("+") + kElementSpacing;
            break;
        default: break;
    }

    if (!ch.extensions().empty())
        rowWidth += modFm.horizontalAdvance(
                        QString::fromStdString(ch.extensions())) + kElementSpacing;

    if (ch.bass_note())
    {
        rowWidth += modFm.horizontalAdvance("/") + kElementSpacing;
        if (ch.bass_note_step())
            rowWidth += modFm.horizontalAdvance(kFlat) + kElementSpacing;
        rowWidth += modFm.horizontalAdvance(QString::number(*ch.bass_note()));
    }

    qreal numberHeight = nmFm.height();
    qreal totalHeight  = numberHeight / kNumberZoneRatio;

    return QSizeF(rowWidth, totalHeight);
}

// ---------------------------------------------------------------------------
// Public: paint
// ---------------------------------------------------------------------------
void ChordRenderer::paint(QPainter& painter,
                          const QRectF& rect,
                          const model::chord& ch,
                          const Fonts& fonts,
                          bool /*is_duration_mode*/)
{
    if (ch.mode() == model::chord::type::UNDEFINED)
        return;

    qreal artHeight = rect.height() * kArticulationZoneRatio;
    QRectF artRect(rect.left(), rect.top(), rect.width(), artHeight);
    QRectF numRect(rect.left(), rect.top() + artHeight,
                   rect.width(), rect.height() - artHeight);

    // Paint number row; get tight rect around the number glyph for diamond
    QRectF numberGlyphRect = paintNumberRow(painter, numRect, ch, fonts);

    // Articulations — order: staccato (top), pushed, tied, diamond (around number)
    if (ch.is_staccato())
        paintStaccato(painter, artRect);

    if (ch.is_pushed())
        paintPushed(painter, artRect, fonts);

    if (ch.is_tied())
        paintTiedArc(painter, artRect);

    if (ch.is_diamond())
        paintDiamond(painter, numberGlyphRect);
}

// ---------------------------------------------------------------------------
// Public: paintRhythm
// ---------------------------------------------------------------------------
void ChordRenderer::paintRhythm(QPainter& painter,
                                 const QRectF& rect,
                                 const model::chord& ch,
                                 const Fonts& fonts)
{
    if (!ch.duration())
        return;

    painter.save();
    painter.setFont(fonts.music);
    QFontMetricsF fm(fonts.music);

    QString glyph = noteGlyph(*ch.duration());
    bool dotted   = isDotted(*ch.duration());

    qreal totalWidth = fm.horizontalAdvance(glyph);
    if (dotted)
        totalWidth += kElementSpacing + fm.ascent() * 0.3;

    qreal x        = rect.left();
    qreal baseline = rect.top() + (rect.height() + fm.ascent() - fm.descent()) / 2.0;

    painter.drawText(QPointF(x, baseline), glyph);
    x += fm.horizontalAdvance(glyph);

    if (dotted)
    {
        qreal dotR = fm.ascent() * 0.15;
        qreal dotX = x + kElementSpacing + dotR;
        qreal dotY = baseline - fm.ascent() * 0.35;
        painter.setBrush(painter.pen().color());
        painter.setPen(Qt::NoPen);
        painter.drawEllipse(QPointF(dotX, dotY), dotR, dotR);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paintNumberRow
// ---------------------------------------------------------------------------
QRectF ChordRenderer::paintNumberRow(QPainter& painter,
                                      const QRectF& rowRect,
                                      const model::chord& ch,
                                      const Fonts& fonts)
{
    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF modFm(fonts.modifier);

    // Compute total row width for centering
    qreal totalWidth = 0;
    if (ch.step())
        totalWidth += modFm.horizontalAdvance(kFlat) + kElementSpacing;
    totalWidth += nmFm.horizontalAdvance(QString::number(ch.number()));
    if (ch.mode() == model::chord::type::MINOR)
        totalWidth += modFm.horizontalAdvance("-") + kElementSpacing;
    else if (ch.mode() == model::chord::type::DIMINISHED)
        totalWidth += modFm.horizontalAdvance(kDiminish) + kElementSpacing;
    else if (ch.mode() == model::chord::type::AUGMENTED)
        totalWidth += modFm.horizontalAdvance("+") + kElementSpacing;
    if (!ch.extensions().empty())
        totalWidth += modFm.horizontalAdvance(
                          QString::fromStdString(ch.extensions())) + kElementSpacing;
    if (ch.bass_note())
    {
        totalWidth += modFm.horizontalAdvance("/") + kElementSpacing;
        if (ch.bass_note_step())
            totalWidth += modFm.horizontalAdvance(kFlat) + kElementSpacing;
        totalWidth += modFm.horizontalAdvance(QString::number(*ch.bass_note()));
    }

    qreal baseline = rowRect.top()
                     + (rowRect.height() + nmFm.ascent() - nmFm.descent()) / 2.0;
    qreal x = rowRect.left();

    painter.save();

    // Step — raised slightly, smaller font
    x += paintStep(painter, ch, fonts, x, baseline - nmFm.ascent() * 0.4);

    // Number — dominant
    painter.setFont(fonts.number);
    QString numStr  = QString::number(ch.number());
    qreal numLeft   = x;
    painter.drawText(QPointF(x, baseline), numStr);
    QRectF numberGlyphRect(numLeft,
                           baseline - nmFm.ascent(),
                           nmFm.horizontalAdvance(numStr),
                           nmFm.ascent() + nmFm.descent());
    x += nmFm.horizontalAdvance(numStr);

    // Mode suffix
    x += paintMode(painter, ch, fonts, x, baseline);

    // Extensions — superscripted
    x += paintExtensions(painter, ch, fonts, x, baseline - nmFm.ascent() * 0.3);

    // Bass note
    paintBassNote(painter, ch, fonts, x, baseline);

    painter.restore();

    return numberGlyphRect;
}

// ---------------------------------------------------------------------------
// Private: paintStep
// ---------------------------------------------------------------------------
qreal ChordRenderer::paintStep(QPainter& painter,
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
    return fm.horizontalAdvance(glyph) + kElementSpacing;
}

// ---------------------------------------------------------------------------
// Private: paintMode
// ---------------------------------------------------------------------------
qreal ChordRenderer::paintMode(QPainter& painter,
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
    qreal raisedBaseline = baseline - nmFm.ascent() + fm.ascent();
    painter.drawText(QPointF(x, raisedBaseline), suffix);
    return fm.horizontalAdvance(suffix) + kElementSpacing;
}

// ---------------------------------------------------------------------------
// Private: paintExtensions
// ---------------------------------------------------------------------------
qreal ChordRenderer::paintExtensions(QPainter& painter,
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
    return fm.horizontalAdvance(ext) + kElementSpacing;
}

// ---------------------------------------------------------------------------
// Private: paintBassNote
// ---------------------------------------------------------------------------
qreal ChordRenderer::paintBassNote(QPainter& painter,
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
    consumed += fm.horizontalAdvance("/") + kElementSpacing;
    x += consumed;

    if (ch.bass_note_step())
    {
        QString g = (ch.bass_note_step() == model::chord::flat_sharp::FLAT)
                    ? QString(kFlat) : QString(kSharp);
        painter.drawText(QPointF(x, baseline), g);
        qreal w = fm.horizontalAdvance(g) + kElementSpacing;
        consumed += w;
        x += w;
    }

    QString bn = QString::number(*ch.bass_note());
    painter.drawText(QPointF(x, baseline), bn);
    consumed += fm.horizontalAdvance(bn);

    return consumed;
}

// ---------------------------------------------------------------------------
// Private: paintStaccato — filled dot above everything
// ---------------------------------------------------------------------------
void ChordRenderer::paintStaccato(QPainter& painter, const QRectF& artRect)
{
    painter.save();
    qreal dotR = artRect.height() * 0.08;
    qreal cx   = artRect.center().x();
    qreal cy   = artRect.top() + artRect.height() * kStaccatoTopRatio + dotR;
    painter.setBrush(painter.pen().color());
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(cx, cy), dotR, dotR);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paintPushed — '>' below staccato dot
// ---------------------------------------------------------------------------
void ChordRenderer::paintPushed(QPainter& painter,
                                 const QRectF& artRect,
                                 const Fonts& fonts)
{
    painter.save();
    painter.setFont(fonts.articulation);
    QFontMetricsF fm(fonts.articulation);
    qreal y = artRect.top() + artRect.height() * kPushedTopRatio + fm.ascent();
    qreal x = artRect.center().x() - fm.horizontalAdvance(">") / 2.0;
    painter.drawText(QPointF(x, y), ">");
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paintTiedArc — curved arc below pushed
// ---------------------------------------------------------------------------
void ChordRenderer::paintTiedArc(QPainter& painter, const QRectF& artRect)
{
    painter.save();
    qreal arcTop = artRect.top() + artRect.height() * kTiedTopRatio;
    qreal arcH   = artRect.height() * 0.25;
    qreal margin = artRect.width() * 0.1;

    QPainterPath path;
    path.moveTo(artRect.left() + margin, arcTop + arcH);
    path.quadTo(artRect.center().x(), arcTop,
                artRect.right() - margin, arcTop + arcH);

    painter.setPen(QPen(painter.pen().color(), 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paintDiamond — drawn around the number glyph rect
// ---------------------------------------------------------------------------
void ChordRenderer::paintDiamond(QPainter& painter, const QRectF& numberRect)
{
    painter.save();
    QRectF r = numberRect.adjusted(-kDiamondPadding, -kDiamondPadding,
                                    kDiamondPadding,  kDiamondPadding);
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
QString ChordRenderer::noteGlyph(model::chord::time duration)
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
        // Whole and half have no safe Unicode glyph.
        // Replace with Bravura U+E1D2 / U+E1D3 if font is available.
        case model::chord::time::WHOLE:
            return QString("o");
        case model::chord::time::HALF:
        case model::chord::time::DOTTED_HALF:
            return QString("d");
        default:
            return QString("?");
    }
}

bool ChordRenderer::isDotted(model::chord::time duration)
{
    return duration == model::chord::time::DOTTED_EIGHTH  ||
           duration == model::chord::time::DOTTED_QUARTER ||
           duration == model::chord::time::DOTTED_HALF;
}

} // namespace nashville::view
