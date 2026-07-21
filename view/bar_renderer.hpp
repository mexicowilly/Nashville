#pragma once

#include "../model/bar.hpp"
#include "chord_renderer.hpp"
#include <QPainter>
#include <QRectF>
#include <vector>
#include <cstddef>
#include <optional>

namespace nashville::view
{

// Stateless renderer for a single bar.
class bar_renderer
{
public:
    // Returns the minimum width needed to render this bar.  The repeat-
    // mark flags grow the width when set so the painted glyphs have
    // dedicated horizontal space at the bar's left/right edges and don't
    // squeeze the chord row.  draw_beat_parens mirrors the flag paint()
    // takes: when set, extra width is reserved so the parenthesis glyphs
    // wrapping a custom beat-count bar's chords have room to sit without
    // encroaching on whatever follows (a neighbouring bar, or the
    // continuation dot after an extended line's last bar).
    static qreal width_hint(const model::bar& bar,
                           qreal height,
                           const chord_renderer::Fonts& fonts,
                           bool draw_begin_repeat = false,
                           bool draw_end_repeat   = false,
                           qreal modulation_slot_w = 0.0,
                           bool draw_beat_parens   = false);

    // Paints the bar into rect.
    // line_duration_mode: true if any bar on this line is duration-mode,
    // which controls whether the rule and rhythm row zones are allocated.
    // draw_begin_repeat / draw_end_repeat: paint a repeat mark at the
    // left / right edge of the bar.  The renderer reserves matching slot
    // widths internally (mirroring width_hint), so the chord row stays
    // visually balanced regardless of which side carries a mark.
    // effective_time_sig: the time signature actually in effect for this
    // bar (the song's default, or the nearest preceding bar-level override
    // — the caller resolves that chain since bar_renderer only ever sees
    // one bar at a time). Used purely to decide how eighth/sixteenth notes
    // in the rhythm row group under beams; irrelevant outside duration mode.
    static void paint(QPainter& painter,
                      const QRectF& rect,
                      const model::bar& bar,
                      const chord_renderer::Fonts& fonts,
                      bool line_duration_mode,
                      bool line_has_articulation,
                      bool draw_begin_repeat = false,
                      bool draw_end_repeat   = false,
                      bool draw_beat_parens  = false,
                      qreal modulation_slot_w = 0.0,
                      const model::time_signature& effective_time_sig = model::time_signature());

    // Returns the y-coordinate at the vertical centre of the number row
    // for a bar laid out into `rect`.  Match this value when placing any
    // adornment that should sit on the chord-number line (e.g. the
    // continuation dot painted between bars when a line exceeds
    // bars_per_line()).  The computation mirrors chord_renderer's
    // numRect.center().y() exactly so the adornment aligns pixel-for-
    // pixel with the chord glyphs.
    static qreal number_row_center_y(const QRectF& rect,
                                     const model::bar& bar,
                                     const chord_renderer::Fonts& fonts,
                                     bool line_duration_mode,
                                     bool line_has_articulation);

    // Width of a repeat-mark slot at the RIGHT edge of a bar (end-repeat).
    // Public so the layout pass can include it in width budgeting and
    // volta-bracket placement.  Sized to give the wings room to extend
    // outward without colliding with the bar's chord column.
    //
    // Begin-repeat does not use a dedicated slot of its own — it is small
    // enough (12px) to draw inside the k_time_sig_slot_w reservation below,
    // which every bar already carries.  See width_hint's comment for why:
    // in short, a real per-bar left slot would shift that one bar's chord
    // number relative to every other bar in its column, since the flag is
    // per-bar while the column width is shared.
    static constexpr qreal k_repeat_slot_w = 12.0;

    // Width always reserved at the left of a bar for a time-signature glyph.
    // Bars without a time sig leave this space empty so chord columns align.
    // Public so paint_beat_dots in song_body_widget can align the dot row
    // with the chord column.
    static constexpr qreal k_time_sig_slot_w = 20.0;

    // Horizontal space consumed by the modulation indicator — the circled
    // key name painted immediately before the chord column on a bar that
    // starts a modulation (non-empty bar.modulation()).  Returns 0 when the
    // bar carries no modulation.  The width tracks the circle diameter,
    // which is sized from `height` so the circle fits within the bar's full
    // vertical extent (articulations + durations included).
    //
    // This is a PER-BAR measurement.  The layout pass takes the maximum
    // across every bar in a column to obtain one uniform per-column slot
    // width, then feeds that value back into width_hint() / paint() /
    // song_body_widget::paint_beat_dots() for EVERY bar in the column —
    // modulating or not.  Reserving the slot column-wide keeps the chord
    // columns aligned across lines (a modulation on one line shifts that
    // column's chords on every line by the same amount) rather than nudging
    // only the modulating bar out of alignment with its column.
    static qreal modulation_slot_width(const model::bar& bar,
                                       qreal height,
                                       const chord_renderer::Fonts& fonts);

    // Vertical split ratios for duration-mode bars (public so callers can derive heights)
    static constexpr qreal k_chord_slot_ratio = 0.60;
    static constexpr qreal k_rule_thickness   = 1.0;
    static constexpr qreal k_rhythm_row_ratio = 0.30;

    // True if this bar should render the rhythm row — the rule plus the
    // duration/rest symbols beneath it.  A bar qualifies when any of its
    // chords carries a duration OR is a rest: rests are inherently rhythmic
    // and always live below the rule (an unset duration renders as a whole
    // rest), so a bar holding one is in duration mode even with no explicit
    // durations.  Public so the layout pass in song_body_widget shares this
    // single definition rather than re-deriving it.
    static bool is_duration_mode(const model::bar& bar);

private:
    // Paints a single repeat mark.  `mark_rect` is the slot the glyph
    // should occupy (width == k_repeat_slot_w); `is_begin` selects which
    // side the dots and wings face.
    //   begin: thick-then-thin bars, dots on the right, wings hooking
    //          to the right of the thick bar (towards the music being
    //          repeated).
    //   end:   thin-then-thick bars, dots on the left, wings hooking
    //          to the left.
    // The "wings" are short angled strokes from the top and bottom of
    // the thick bar; Nashville charts use them because there is no
    // staff to anchor the dots' vertical position, and the wings
    // visually compensate.
    static void paint_repeat_mark(QPainter& painter,
                                  const QRectF& mark_rect,
                                  bool is_begin);

    // Paints the modulation indicator: the bar's modulation() key name
    // inside a thin black circle, mirroring the key circle in the song
    // margin but sized down to fit the bar.  The circle is centred
    // vertically in `bar_rect` (the full bar height, so it sits midway
    // through the articulation + number + duration stack) and horizontally
    // in the slot whose left edge is `slot_left`.  Drawn only when the bar
    // has a non-empty modulation; callers gate on modulation_slot_width().
    static void paint_modulation(QPainter& painter,
                                 const QRectF& bar_rect,
                                 qreal slot_left,
                                 const model::bar& bar,
                                 const chord_renderer::Fonts& fonts);

    // Width given to the (otherwise zero-width) chord row of an empty
    // bar — i.e. one with no chords yet, as produced by the "Insert 1
    // before/after" menu actions.  Sized to roughly one chord-glyph
    // worth of horizontal space so the new bar reads as a recognisable
    // column in the layout and is comfortably click-able once the user
    // decides to edit it.  A genuinely zero-width slot would only show
    // through k_bar_padding as a thin gap, which isn't enough visual
    // signal that a bar landed there.
    static constexpr qreal k_empty_bar_chord_slot_w = 20.0;

    // Horizontal gap between chords within a bar
    static constexpr qreal k_inter_chord_spacing = 6.0;

    // --- Beaming ---
    //
    // A beam_group is a contiguous run of chord indices (into
    // bar.chords()) — start and end both inclusive — that should share a
    // beam instead of each carrying its own eighth/sixteenth flag. Only
    // runs of two or more notes are ever recorded here; an isolated
    // beamable note keeps its individual flag and never appears in this
    // list. See compute_beam_groups() for the grouping rules themselves.
    struct beam_group
    {
        std::size_t start = 0;
        std::size_t end   = 0;
    };

    // Works out how eighth/sixteenth notes in `chords` should be grouped
    // under beams, given the beat structure implied by `ts`. Rules
    // (confirmed with the person requesting this feature, since beaming
    // conventions have real disagreement between sources):
    //   - Never beam across a bar line (moot here — one call is one bar).
    //   - Sixteenth notes are always grouped by beat: a run containing a
    //     sixteenth is confined to the single beat it started in, and
    //     never merges with a neighbouring beat's run, whatever that
    //     neighbour contains.
    //   - A run made up entirely of eighths/dotted-eighths (no sixteenth)
    //     MAY merge with the adjacent beat's run within the same half of
    //     the bar — e.g. beats 1+2 or 3+4 in 4/4 — giving the familiar
    //     "four eighths under one beam" look. It never merges across the
    //     bar's primary division (beat 2 into beat 3 in 4/4).
    //   - A rest, or a chord with no duration set, breaks any run it
    //     falls inside (rests are not beamed over).
    //   - Compound meters (8-kind, count a multiple of 3 and >= 6, e.g.
    //     6/8, 9/8, 12/8) use a dotted-quarter beat (3 eighths / 6
    //     sixteenths) and never merge beyond that one compound beat —
    //     that already matches "group by beat" for the sixteenth case,
    //     and matches the standard "two groups of three" look for 6/8
    //     eighths, so no separate merge pass is needed for them.
    //   - The eighth-merge pass above only applies to simple meters
    //     (quarter- or half-note beat) with an even number of beats;
    //     odd beat counts (3/4 etc.) fall back to one group per beat,
    //     which every source treats as always correct even where a
    //     wider grouping would also be acceptable.
    //   - A merge is only allowed on the same side of the bar's true
    //     mid-point (beat_count / 2), computed directly rather than
    //     inferred from beat_index parity — the two agree whenever
    //     beat_count is a multiple of 4 (4/4, 8/4...), but parity alone
    //     would incorrectly permit a merge straight across the middle
    //     for other even beat counts (e.g. 6/4, midpoint at beat 3).
    // beat_count_override: a bar's own number_of_beats(), when set —
    // pickup/partial bars have a different beat count (and therefore a
    // different mid-point) than the time signature's nominal count, even
    // though how long one beat lasts (beat_16ths) doesn't change.
    static std::vector<beam_group> compute_beam_groups(
        const std::vector<model::chord>& chords,
        const model::time_signature& ts,
        const std::optional<unsigned>& beat_count_override = std::nullopt);

    // Draws one beam (primary line, plus any secondary sixteenth-level
    // segments/partial stubs) across the notes in `group`. `stems` holds
    // the geometry chord_renderer::paint_rhythm handed back for every
    // chord in the bar, indexed the same way as `chords`.
    static void paint_beam(QPainter& painter,
                           const std::vector<model::chord>& chords,
                           const std::vector<chord_renderer::StemInfo>& stems,
                           const beam_group& group);

    // --- Beat-safe note splitting ---
    //
    // Standard notation rule: a note that doesn't start exactly on a beat
    // may not be notated as a single symbol if doing so would extend to or
    // across the next beat boundary — that would obscure where the next
    // beat starts. Instead it has to be written as multiple tied notes,
    // each confined to (or starting cleanly on) a beat. E.g. in 4/4, an
    // eighth followed by what would naturally be a dotted quarter starting
    // on the "and" of beat 1: the dotted quarter can't stand as one note
    // (it would run from the and-of-1 through all of beat 2), so it's
    // rendered as an eighth (finishing out beat 1) tied to a quarter
    // (starting cleanly on beat 2) — the same total duration, correctly
    // spelled. This applies per the person's direction to every beat
    // boundary and every note value, not just the bar's mid-point.
    //
    // A rhythm_unit is one piece of that spelling — for a chord that
    // doesn't need splitting (the overwhelmingly common case), a chord
    // produces exactly one unit equal to itself.
    struct rhythm_unit
    {
        std::size_t  chord_index = 0;   // which bar.chords() entry this came from
        model::chord piece;             // synthetic chord carrying only this piece's
                                         // duration and rest-ness — chord_renderer's
                                         // rhythm-row painting only ever looks at
                                         // is_rest()/duration(), so this is sufficient
                                         // to paint it correctly standing alone
        bool first_piece  = true;       // only the first piece of a chord is real
                                         // enough to matter beyond the rhythm row —
                                         // callers use this to avoid, say, re-painting
                                         // articulations once per piece
        bool tie_to_next  = false;      // true if a tie arc belongs between this
                                         // unit's notehead and the next unit's
    };

    // Splits `chords` into rhythm_units, one-to-one where no splitting is
    // needed. Rests are never split (a rest has no attack point to
    // obscure, and this app's rests are already whole-bar-friendly via
    // leger-line glyphs) — nor is a chord with no duration set. beat_16ths
    // is the length of one beat, in sixteenth-note units (see
    // compute_beam_groups for how that's derived from a time signature).
    static std::vector<rhythm_unit> expand_rhythm_units(
        const std::vector<model::chord>& chords,
        unsigned beat_16ths);

    // Length of one beat, in sixteenth-note units, implied by `ts` (and,
    // for compound meters, whether count is a multiple of 3 — see
    // compute_beam_groups's header comment). Shared by compute_beam_groups
    // and expand_rhythm_units so the two can never disagree about where
    // beat boundaries fall.
    static unsigned compute_beat_16ths(const model::time_signature& ts);

    // Draws a short tie arc in the rhythm row connecting two adjacent
    // rhythm-unit noteheads that are pieces of the same original note —
    // NOT the pre-existing chord-level is_tied() arc (that's a separate,
    // unrelated indicator drawn in the number row for a tie between two
    // different chords). from_x/to_x are the two noteheads' stem_x
    // positions; row_bottom is the rhythm row's baseline.
    static void paint_split_tie(QPainter& painter, qreal from_x, qreal to_x,
                                qreal row_bottom);
};

} // namespace nashville::view
