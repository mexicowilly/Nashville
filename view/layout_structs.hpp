#pragma once

#include "../model/bar.hpp"
#include <QRectF>
#include <QString>
#include <vector>
#include <set>
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

    // --- Repeat marks ---
    // These are derived from the model's repeat() flag at layout time and
    // from the volta analysis: a non-final volta ends with an implicit end
    // repeat even when the model doesn't carry one (see
    // bar::repeat_status::END), matching standard music notation practice.
    // Painted by bar_renderer at the bar's left/right edges respectively.
    bool draw_begin_repeat = false;
    bool draw_end_repeat   = false;
};

// One contiguous run of bars that share the same set of volta numbers.
// Voltas live in their own visual zone above the chord row and are
// rendered as labelled horizontal brackets — see paint_volta_brackets()
// in song_body_widget.
struct volta_span
{
    std::size_t first_bar_index = 0;   // index into line_layout::bars
    std::size_t last_bar_index  = 0;   // inclusive
    std::set<unsigned> numbers;        // copy of the bars' voltas() set
    // True if a closing right-hook should be drawn on the bracket.  A
    // closed bracket says "go back and repeat" (the implicit end-repeat
    // sits beneath it).  The final volta of a repeat section is open —
    // playback falls through to the next bar rather than looping.
    bool closed = true;
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
    bool has_voltas            = false;     // true if any bar on this line carries a volta number
    std::vector<volta_span> volta_spans;    // grouped runs of bars sharing a volta set
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
