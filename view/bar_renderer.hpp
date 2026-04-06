#pragma once

#include "../model/bar.hpp"
#include "chord_renderer.hpp"
#include <QPainter>
#include <QRectF>

namespace nashville::view
{

// Stateless renderer for a single bar.
class BarRenderer
{
public:
    // Returns the minimum width needed to render this bar.
    static qreal widthHint(const model::bar& bar,
                           qreal height,
                           const ChordRenderer::Fonts& fonts);

    // Paints the bar into rect.
    // lineDurationMode: true if any bar on this line is duration-mode,
    // which controls whether the rule and rhythm row zones are allocated.
    static void paint(QPainter& painter,
                      const QRectF& rect,
                      const model::bar& bar,
                      const ChordRenderer::Fonts& fonts,
                      bool lineDurationMode);

private:
    static bool isDurationMode(const model::bar& bar);

    // Fixed horizontal slot reserved for a time signature at the left of every bar.
    // Bars without a time sig leave this space empty so chord columns stay aligned.
    static constexpr qreal kTimeSigSlotW = 20.0;

    // Horizontal gap between chords within a bar
    static constexpr qreal kInterChordSpacing = 6.0;

    // Vertical split ratios for duration-mode bars
    static constexpr qreal kChordSlotRatio = 0.60;
    static constexpr qreal kRuleThickness  = 1.0;
    static constexpr qreal kRhythmRowRatio = 0.30;
};

} // namespace nashville::view
