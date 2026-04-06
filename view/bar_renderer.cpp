#include "bar_renderer.hpp"
#include <QFontMetricsF>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// Public: widthHint
// ---------------------------------------------------------------------------
qreal BarRenderer::widthHint(const model::bar& bar,
                              qreal /*height*/,
                              const ChordRenderer::Fonts& fonts)
{
    if (bar.empty())
        return 0.0;

    qreal totalWidth = 0.0;
    for (const auto& ch : bar.chords())
        totalWidth += ChordRenderer::sizeHint(ch, fonts).width();

    totalWidth += kInterChordSpacing * (bar.chords().size() - 1);
    totalWidth += kTimeSigSlotW;
    return totalWidth;
}

// ---------------------------------------------------------------------------
// Public: paint
// ---------------------------------------------------------------------------
void BarRenderer::paint(QPainter& painter,
                         const QRectF& rect,
                         const model::bar& bar,
                         const ChordRenderer::Fonts& fonts,
                         bool lineDurationMode)
{
    if (bar.empty())
        return;

    bool durationMode = isDurationMode(bar);

    qreal topPad     = rect.height() * 0.05;
    qreal chordSlotH = rect.height() * kChordSlotRatio;
    qreal rhythmRowH = rect.height() * kRhythmRowRatio;

    // --- Time signature: always reserve kTimeSigSlotW so chord columns align ---
    // Paint glyphs only when this bar actually has a time sig change.
    qreal chordsLeft = rect.left() + kTimeSigSlotW;

    if (bar.time_sig())
    {
        const auto& ts = *bar.time_sig();
        QFont tsFont = fonts.modifier;
        QFontMetricsF fm(tsFont);

        QString countStr = QString::number(ts.count());
        QString kindStr  = QString::number(static_cast<int>(ts.kind()));
        qreal maxAdv = std::max(fm.horizontalAdvance(countStr),
                                fm.horizontalAdvance(kindStr));
        qreal tsX    = rect.left() + (kTimeSigSlotW - maxAdv) / 2.0;  // centred in slot
        qreal tsY    = rect.top() + topPad;
        qreal sep    = 3.0;

        // Numerator baseline = tsY + ascent
        painter.save();
        painter.setFont(tsFont);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawText(QPointF(tsX + (maxAdv - fm.horizontalAdvance(countStr)) / 2.0,
                                 tsY + fm.ascent()),
                         countStr);
        painter.restore();

        // Separator: sits sep px below the bottom of the numerator glyph
        qreal lineY = std::round(tsY + fm.ascent() + fm.descent() + sep);
        painter.fillRect(QRectF(tsX, lineY, maxAdv, 1.0), Qt::black);

        // Denominator baseline = lineY + sep + ascent
        painter.save();
        painter.setFont(tsFont);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawText(QPointF(tsX + (maxAdv - fm.horizontalAdvance(kindStr)) / 2.0,
                                 lineY + sep + fm.ascent()),
                         kindStr);
        painter.restore();
    }

    QRectF chordSlotRect(chordsLeft,
                          rect.top() + topPad,
                          rect.width() - kTimeSigSlotW,
                          chordSlotH);

    // Compute per-chord widths
    std::vector<qreal> chordWidths;
    qreal totalChordWidth = 0.0;
    for (const auto& ch : bar.chords())
    {
        qreal w = ChordRenderer::sizeHint(ch, fonts).width();
        chordWidths.push_back(w);
        totalChordWidth += w;
    }

    qreal availableWidth = chordSlotRect.width()
                           - kInterChordSpacing * (bar.chords().size() - 1);
    qreal scale = (totalChordWidth > 0.0) ? availableWidth / totalChordWidth : 1.0;

    qreal x = chordsLeft;
    qreal lastChordRight = chordsLeft;
    for (std::size_t i = 0; i < bar.chords().size(); ++i)
    {
        qreal slotWidth = chordWidths[i] * scale;
        QRectF slotRect(x, chordSlotRect.top(), slotWidth, chordSlotH);

        ChordRenderer::paint(painter, slotRect, bar.chords()[i], fonts, durationMode);
        lastChordRight = x + chordWidths[i];  // actual glyph right edge

        if (durationMode && lineDurationMode)
        {
            qreal ruleY = rect.top() + topPad + chordSlotH + kRuleThickness / 2.0;
            QRectF rhythmRect(x, ruleY + kRuleThickness, slotWidth, rhythmRowH);
            ChordRenderer::paintRhythm(painter, rhythmRect, bar.chords()[i], fonts);
        }

        x += slotWidth + kInterChordSpacing;
    }

    // Single underline from chords-left to just past the last chord glyph
    if (bar.chords().size() > 1)
    {
        constexpr qreal kUnderlineOverhang = 4.0;
        qreal underlineY = chordSlotRect.top() + chordSlotH + 1.5;
        painter.save();
        painter.setPen(QPen(Qt::black, 0.75));
        painter.drawLine(QPointF(chordsLeft,                         underlineY),
                         QPointF(lastChordRight + kUnderlineOverhang, underlineY));
        painter.restore();
    }

    // Bar-wide horizontal rule for duration-mode bars
    if (durationMode && lineDurationMode)
    {
        qreal ruleY = rect.top() + topPad + chordSlotH + kRuleThickness / 2.0;
        painter.save();
        painter.setPen(QPen(painter.pen().color(), kRuleThickness));
        painter.drawLine(QPointF(rect.left(),  ruleY),
                         QPointF(rect.right(), ruleY));
        painter.restore();
    }
}

// ---------------------------------------------------------------------------
// Private: isDurationMode
// ---------------------------------------------------------------------------
bool BarRenderer::isDurationMode(const model::bar& bar)
{
    if (bar.empty())
        return false;
    // Model guarantees no mixed bars — check only the first chord.
    return bar.chords().front().duration().has_value();
}

} // namespace nashville::view
