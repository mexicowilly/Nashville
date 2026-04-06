#include "margin_renderer.hpp"
#include <QFontMetricsF>
#include <QFont>
#include <cmath>

namespace nashville::view
{

static constexpr const char* kFlat  = "\u266D"; // ♭
static constexpr const char* kSharp = "\u266F"; // ♯

void MarginRenderer::paint(QPainter& painter,
                            const QRectF& marginRect,
                            const model::song& song)
{
    painter.save();

    QFont baseFont("Georgia", 13);
    QFont keyFont("Georgia", 16, QFont::Bold);
    QFontMetricsF baseFm(baseFont);
    QFontMetricsF keyFm(keyFont);

    qreal cx = marginRect.center().x();
    qreal y  = marginRect.top() + kTopPadding;

    // ----------------------------------------------------------------
    // 1. Key — letter (+ accidental) inside a circle
    // ----------------------------------------------------------------
    QString keyLetter, keyAccidental;
    parseKey(song.key(), keyLetter, keyAccidental);
    QString keyDisplay = keyLetter + keyAccidental;

    painter.setFont(keyFont);
    qreal keyTextW = keyFm.horizontalAdvance(keyDisplay);
    qreal keyTextH = keyFm.height();
    qreal circleR  = std::max(keyTextW, keyTextH) / 2.0 + kCirclePadding;

    QPointF circleCenter(cx, y + circleR);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 1.5));
    painter.drawEllipse(circleCenter, circleR, circleR);

    qreal keyX        = circleCenter.x() - keyTextW / 2.0;
    qreal keyBaseline = circleCenter.y()
                        + (keyFm.ascent() - keyFm.descent()) / 2.0;
    painter.drawText(QPointF(keyX, keyBaseline), keyDisplay);

    y += circleR * 2.0 + kElementSpacing;

    // ----------------------------------------------------------------
    // 2. Time signature — stacked numerals, centered
    // ----------------------------------------------------------------
    const auto& ts = song.time_sig();
    QString countStr = QString::number(ts.count());
    QString kindStr  = QString::number(static_cast<int>(ts.kind()));

    painter.setFont(baseFont);
    qreal tsNumW = std::max(baseFm.horizontalAdvance(countStr),
                            baseFm.horizontalAdvance(kindStr));

    qreal countX = cx - baseFm.horizontalAdvance(countStr) / 2.0;
    qreal kindX  = cx - baseFm.horizontalAdvance(kindStr)  / 2.0;

    qreal rowH    = baseFm.ascent() + baseFm.descent();  // full glyph height
    qreal sep     = 3.0;                                  // gap below glyph and above denominator
    qreal sepLeft = cx - tsNumW / 2.0;

    painter.drawText(QPointF(countX, y + baseFm.ascent()), countStr);

    // Separator line — centred, 1px thick, with breathing room above and below
    qreal lineY = std::round(y + rowH + sep);
    painter.fillRect(QRectF(sepLeft, lineY, tsNumW, 1.0), Qt::black);

    painter.drawText(QPointF(kindX, lineY + sep + baseFm.ascent()), kindStr);

    y += rowH + sep + 1.0 + sep + rowH + kElementSpacing;

    // ----------------------------------------------------------------
    // 3. Tempo — note glyph + " = " + BPM, centered
    // ----------------------------------------------------------------
    auto [bpm, beatUnit] = song.tempo();
    QString tempoStr = tempoGlyph(beatUnit) + " = " + QString::number(bpm);

    painter.setFont(baseFont);
    qreal tempoX = cx - baseFm.horizontalAdvance(tempoStr) / 2.0;
    painter.drawText(QPointF(tempoX, y + baseFm.ascent()), tempoStr);

    painter.restore();
}

// ---------------------------------------------------------------------------
// parseKey
// ---------------------------------------------------------------------------
void MarginRenderer::parseKey(const std::string& key,
                               QString& letter,
                               QString& accidental)
{
    if (key.empty())
    {
        letter     = "?";
        accidental = "";
        return;
    }

    letter     = QString(QChar(key[0])).toUpper();
    accidental = "";

    if (key.size() > 1)
    {
        char acc = key[1];
        if (acc == 'b' || acc == 'B')
            accidental = QString(kFlat);
        else if (acc == '#')
            accidental = QString(kSharp);
    }
}

// ---------------------------------------------------------------------------
// tempoGlyph
// ---------------------------------------------------------------------------
QString MarginRenderer::tempoGlyph(model::chord::time beatUnit)
{
    switch (beatUnit)
    {
        case model::chord::time::QUARTER:
        case model::chord::time::DOTTED_QUARTER:
            return "\u2669"; // ♩
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:
            return "\u266A"; // ♪
        case model::chord::time::HALF:
        case model::chord::time::DOTTED_HALF:
            return "h";      // replace with Bravura U+E1D3 if available
        case model::chord::time::WHOLE:
            return "o";      // replace with Bravura U+E1D2 if available
        default:
            return "\u2669";
    }
}

} // namespace nashville::view
