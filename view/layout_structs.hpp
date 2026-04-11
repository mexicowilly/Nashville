#pragma once

#include "../model/bar.hpp"
#include <QRectF>
#include <QString>
#include <vector>
#include <optional>

namespace nashville::view
{

struct bar_layout
{
    const model::bar* bar               = nullptr;
    QRectF rect;
    bool is_duration_mode               = false;  // cached from bar contents
    bool show_continuation_dot         = false;  // paint dot in gap after this bar
};

struct line_layout
{
    std::vector<bar_layout> bars;
    QRectF rect;                            // full line rect including all bars
    QRectF section_col_rect;                // column to the left of bars for section label
    std::optional<QString> section_label;   // from first bar that has section_
    bool is_duration_mode      = false;     // true if ANY bar on this line is duration-mode
    bool draw_section_end_rule = false;     // draw gray rule below this line (section boundary)
    bool has_articulation      = false;     // true if ANY chord on this line has a push/staccato
};

} // namespace nashville::view
