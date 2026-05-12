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
    qreal num_center_y                  = 0.0;    // vertical centre of number row, for dot placement
};

struct line_layout
{
    std::vector<bar_layout> bars;
    QRectF rect;                            // full line rect including all bars
    QRectF section_col_rect;                // column to the left of bars for section label
    std::optional<QString> section_label;   // from first bar that has section_
    bool is_duration_mode      = false;     // true if ANY bar on this line is duration-mode
    bool draw_section_end_rule = false;     // draw gray rule below this line (section boundary)
    bool has_articulation      = false;     // true if ANY chord on this line has a push/staccato/tie
};

// Where a new bar may be inserted by clicking an empty "ghost" rectangle.
// At most three kinds exist; at any moment the layout has 0, 1, or 2 slots:
//   * first_bar  — empty song; appending creates the song's first bar.
//   * same_line  — append after the last bar, on the same visual line.
//                  Requires clearing is_eol on the previous last bar so the
//                  new bar joins it rather than starting a new line.
//   * next_line  — append below the last line.  Requires setting is_eol on
//                  the previous last bar so the new bar starts a new line.
enum class insertion_slot_kind
{
    first_bar,
    same_line,
    next_line,
};

struct insertion_slot
{
    QRectF rect;
    insertion_slot_kind kind = insertion_slot_kind::first_bar;
};

} // namespace nashville::view
