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

    // --- Custom beat count ---
    // Set when this bar's number_of_beats() differs from the song's time
    // signature count.  draw_beat_parens drives the bar_renderer to wrap
    // every chord number in parentheses; beat_count drives song_body_widget
    // to paint the dot row in the zone above the bar rect.
    bool draw_beat_parens = false;
    unsigned beat_count   = 0;     // number of dots to draw (valid iff draw_beat_parens)

    // --- Modulation indicator ---
    // Column-wide reserved width for the circled key name that marks a
    // modulation.  Computed by the layout as the max modulation slot over
    // every bar in this column, then applied to EVERY bar in the column so
    // the chords shift uniformly and stay aligned across lines (only bars
    // whose model carries a modulation actually paint a circle).  Zero when
    // no bar in the column starts a modulation.
    qreal modulation_slot_w = 0.0;

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
    bool has_beat_dots         = false;     // true if any bar on this line has a custom beat count
    std::vector<volta_span> volta_spans;    // grouped runs of bars sharing a volta set
};

// Where a new bar may be inserted by clicking an empty "ghost" rectangle.
// Slots are placed by compute_insertion_slots; their kind determines what
// the click does, their insert_at gives the song-bar index where the new
// bar lands on commit.
//   * first_bar  — empty song; appending creates the song's first bar.
//                  insert_at is 0.
//   * same_line  — extend a specific line by one bar.  The line is the
//                  one whose last bar lives at insert_at - 1 in the song
//                  vector.  Commit transfers is_eol from that previous
//                  tail to the new bar (so the line still ends in the
//                  right place, just with one more bar in it).  Slots of
//                  this kind appear at the right edge of every line —
//                  not only the last line of the song — so any line can
//                  be extended via the mouse.
//   * next_line  — append below the last line of the song.  insert_at is
//                  bars.size().  Requires setting is_eol on the previous
//                  last bar so the new bar starts a new line.  Only ever
//                  generated for the last line — middle lines already
//                  have a next line, so no affordance is needed.
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
    // Song-bar index where the new bar lands on commit.  For first_bar
    // this is 0; for next_line this is bars.size(); for same_line this
    // is (line's last bar's song index) + 1.
    std::size_t insert_at = 0;
};

} // namespace nashville::view
