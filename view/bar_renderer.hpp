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
    // Returns the minimum width needed to render this bar.
    static qreal width_hint(const model::bar& bar,
                           qreal height,
                           const chord_renderer::Fonts& fonts);

    // Paints the bar into rect.
    // line_duration_mode: true if any bar on this line is duration-mode,
    // which controls whether the rule and rhythm row zones are allocated.
    static void paint(QPainter& painter,
                      const QRectF& rect,
                      const model::bar& bar,
                      const chord_renderer::Fonts& fonts,
                      bool line_duration_mode,
                      bool line_has_articulation);

    // Vertical split ratios for duration-mode bars (public so callers can derive heights)
    static constexpr qreal k_chord_slot_ratio = 0.60;
    static constexpr qreal k_rule_thickness   = 1.0;
    static constexpr qreal k_rhythm_row_ratio = 0.30;

private:
    static bool is_duration_mode(const model::bar& bar);

    // Fixed horizontal slot reserved for a time signature at the left of every bar.
    // Bars without a time sig leave this space empty so chord columns stay aligned.
    static constexpr qreal k_time_sig_slot_w = 20.0;

    // Horizontal gap between chords within a bar
    static constexpr qreal k_inter_chord_spacing = 6.0;


};

} // namespace nashville::view
