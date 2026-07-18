#include "bar_renderer.hpp"
#include <QFontMetricsF>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

namespace nashville::view
{

// Forward declaration: defined below alongside the other beat-arithmetic
// helpers, but needed by paint() which appears earlier in this file.
static unsigned duration_in_16ths(model::chord::time t);

// ---------------------------------------------------------------------------
// Repeat-mark geometry
// ---------------------------------------------------------------------------
// These constants describe the painted glyph (NOT the slot width — see
// k_repeat_slot_w in the header).  The thick bar carries the visual
// weight; the thin bar mirrors a standard barline; the dots sit between
// the two bars on the side facing the music being repeated.  Wings hook
// outward from the thick bar in the same direction as the dots; they
// substitute for the staff lines that would normally anchor the dots'
// vertical position in conventional notation.
namespace {
constexpr qreal k_thick_w   = 3.0;   // thick vertical bar
constexpr qreal k_thin_w    = 1.0;   // thin vertical bar
constexpr qreal k_bars_gap  = 3.0;   // gap between thick and thin
constexpr qreal k_dot_r     = 1.6;   // dot radius
constexpr qreal k_dot_inset = 4.0;   // dot offset from the bar's centre
constexpr qreal k_wing_len  = 6.0;   // length of each wing stroke
constexpr qreal k_wing_dy   = 3.0;   // vertical drop from bar end to wing tip

// --- Modulation indicator geometry ---------------------------------------
// The modulation key name is drawn inside a thin black circle, just like the
// key circle in the song margin, but shrunk so it fits within the bar's
// vertical extent.  These constants describe that fit:
constexpr qreal k_mod_v_gutter   = 2.0;  // clearance above & below the circle
constexpr qreal k_mod_text_pad   = 3.0;  // padding from text to circle edge (radius)
constexpr qreal k_mod_chord_gap  = 4.0;  // gap between circle and first chord
constexpr qreal k_mod_min_pt     = 6.0;  // floor font size before we stop shrinking
constexpr qreal k_mod_min_radius = 4.0;  // never collapse the circle below this

// Resolved geometry for one bar's modulation indicator.  `present` is false
// when the bar carries no modulation, in which case the other fields are
// unused.  compute_mod_layout sizes the font down (from the chord modifier
// font) until the circle fits within `height`, mirroring the margin key's
// shrink-to-fit behaviour.
struct mod_layout
{
    bool    present = false;
    QFont   font;
    qreal   radius  = 0.0;
    qreal   text_w  = 0.0;
    QString text;
};

// Convert a raw modulation key string (as stored in the model, e.g. "Bb",
// "F#m") into the display form shown in the circle.  This mirrors
// margin_renderer::parse_key exactly so a modulation reads identically to
// the margin key circle: the note letter is upper-cased and a flat/sharp
// marker immediately following it becomes the proper Unicode glyph
// (♭ / ♯).  The rest of the string (mode suffix etc.) is preserved as-is.
// The substitution is deliberately targeted at that one accidental
// position rather than every 'b'/'#' in the string, so suffixes like
// "bebop" or arbitrary text are never mangled.  Done in the view only —
// the model keeps the user's literal "Bb" / "F#" spelling.
QString format_modulation_display(const std::string& raw)
{
    if (raw.empty())
        return QString();

    static constexpr QChar k_flat (0x266D);  // ♭
    static constexpr QChar k_sharp(0x266F);  // ♯

    QString out;
    out += QChar(QChar(static_cast<ushort>(
                   static_cast<unsigned char>(raw[0]))).toUpper());

    std::size_t consumed = 1;
    if (raw.size() > 1)
    {
        char acc = raw[1];
        if (acc == 'b' || acc == 'B') { out += k_flat;  consumed = 2; }
        else if (acc == '#')          { out += k_sharp; consumed = 2; }
    }
    if (consumed < raw.size())
        out += QString::fromStdString(raw.substr(consumed));
    return out;
}

mod_layout compute_mod_layout(const nashville::model::bar& bar,
                              qreal height,
                              const nashville::view::chord_renderer::Fonts& fonts)
{
    mod_layout ml;
    if (bar.modulation().empty())
        return ml;

    ml.present = true;
    ml.text    = format_modulation_display(bar.modulation());

    // Cap the circle so its diameter stays within the bar's vertical height
    // (less a small gutter top and bottom).  The text font then shrinks from
    // the chord modifier size until the natural circle radius fits the cap.
    const qreal max_radius = std::max(k_mod_min_radius,
                                      height / 2.0 - k_mod_v_gutter);

    QFont f = fonts.modifier;
    qreal try_pt   = f.pointSizeF();
    qreal natural_r = 0.0;
    while (true)
    {
        f.setPointSizeF(try_pt);
        QFontMetricsF fm(f);
        qreal tw = fm.horizontalAdvance(ml.text);
        qreal th = fm.height();
        natural_r = std::max(tw, th) / 2.0 + k_mod_text_pad;
        if (natural_r <= max_radius || try_pt <= k_mod_min_pt)
            break;
        try_pt -= 0.5;
    }

    f.setPointSizeF(std::max(try_pt, k_mod_min_pt));
    QFontMetricsF fm(f);
    ml.font   = f;
    ml.text_w = fm.horizontalAdvance(ml.text);
    ml.radius = std::min(natural_r, max_radius);
    return ml;
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// Public: modulation_slot_width
// ---------------------------------------------------------------------------
qreal bar_renderer::modulation_slot_width(const model::bar& bar,
                                          qreal height,
                                          const chord_renderer::Fonts& fonts)
{
    mod_layout ml = compute_mod_layout(bar, height, fonts);
    if (!ml.present)
        return 0.0;
    // Circle diameter plus the gap to the first chord.
    return ml.radius * 2.0 + k_mod_chord_gap;
}

// ---------------------------------------------------------------------------
// Public: width_hint
// ---------------------------------------------------------------------------
qreal bar_renderer::width_hint(const model::bar& bar,
                              qreal /*height*/,
                              const chord_renderer::Fonts& fonts,
                              bool draw_begin_repeat,
                              bool draw_end_repeat,
                              qreal modulation_slot_w,
                              bool draw_beat_parens)
{
    if (bar.empty())
    {
        // Empty bars are produced by the "Insert 1 before/after" menu
        // actions, which add a chord-less bar to the model so the user
        // can see the layout settle before they choose to edit it.
        // Reporting 0 here would collapse the slot to its k_bar_padding
        // sliver — visible only as a thin gap — which doesn't read as
        // "a bar landed here."  Reserve the time-sig slot plus one
        // chord's worth of width so the new bar occupies a recognisable
        // column.  Repeat-mark slots still grow the width when set, in
        // case the empty bar inherits a BEGIN/END flag from a follow-up
        // edit.
        qreal w = k_time_sig_slot_w + k_empty_bar_chord_slot_w;
        if (draw_begin_repeat) w += k_repeat_slot_w;
        if (draw_end_repeat)   w += k_repeat_slot_w;
        w += modulation_slot_w;
        return w;
    }

    qreal total_width = 0.0;
    for (const auto& ch : bar.chords())
        total_width += chord_renderer::size_hint(ch, fonts).width();

    total_width += k_inter_chord_spacing * (bar.chords().size() - 1);
    total_width += k_time_sig_slot_w;

    // Modulation indicator (circled key name) sits between the time-sig slot
    // and the chord column.  The width is the column-wide reservation passed
    // in by the layout (max over the column), NOT this bar's own circle, so
    // every bar in the column reserves the same space and the chords stay
    // aligned across lines.
    total_width += modulation_slot_w;

    // Repeat marks each consume a dedicated slot so the chord row never
    // ends up sharing horizontal space with the dots and wings.  Width
    // is identical on both sides for visual symmetry.
    if (draw_begin_repeat) total_width += k_repeat_slot_w;
    if (draw_end_repeat)   total_width += k_repeat_slot_w;

    // Beat-count parentheses.  paint() clamps this bar's chord-scaling to
    // never exceed 1.0 (see the scale computation there), so a beat-
    // parens bar never stretches to fill extra column width — the parens
    // always hug their content tightly at a fixed offset from
    // chords_left.  The opening '(' fits inside the existing
    // k_time_sig_slot_w reservation without needing anything extra here
    // (paren glyph + 2px gap is comfortably under that slot's width).
    //
    // The closing ')' is different: it sits past the last chord, inside
    // whatever trailing slack k_bar_padding gives this bar's column —
    // which is enough to avoid clipping, but eats into the same budget
    // that would otherwise be an ordinary bar's full breathing room
    // before the next thing (crucially, the continuation dot after an
    // extended line's last bar).  Reserve just the closing glyph's own
    // width so a beat-parens bar's trailing gap comes out close to an
    // ordinary bar's, rather than measurably tighter.
    //
    // Deliberately NOT reserving for the opening paren too (a bar with
    // beat parens used to reserve both sides here): this column width is
    // maxed across every line sharing the column, so anything reserved
    // for one line's beat-parens bar pushes every other line's
    // continuation dot in that column rightward too. Reserving only what
    // the closing paren actually needs — instead of double-counting both
    // glyphs — keeps that shared impact as small as it can be while
    // still closing most of the gap.
    if (draw_beat_parens)
    {
        QFontMetricsF pfm(fonts.number);
        constexpr qreal k_paren_gap = 2.0;
        total_width += pfm.horizontalAdvance(")") + k_paren_gap;
    }

    return total_width;
}

// ---------------------------------------------------------------------------
// Public: number_row_center_y
// ---------------------------------------------------------------------------
// Mirrors the chord_renderer numRect computation so external callers can
// align adornments (notably the inter-bar continuation dot) with the
// chord-number row.  Any change to top_pad, k_plain_top_pad, or the
// chord-slot height formula in paint() must be reflected here too — the
// two pieces of code share the same geometry contract.
qreal bar_renderer::number_row_center_y(const QRectF& rect,
                                       const model::bar& bar,
                                       const chord_renderer::Fonts& fonts,
                                       bool line_duration_mode,
                                       bool line_has_articulation)
{
    constexpr qreal top_pad         = 2.0;  // matches paint()
    constexpr qreal k_plain_top_pad = 4.0;  // matches chord_renderer

    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF artFm(fonts.articulation);

    qreal num_h = nmFm.ascent() + nmFm.descent();
    qreal art_h = line_has_articulation
                  ? artFm.ascent() + artFm.descent()
                  : 0.0;

    // Does any chord in this bar have a diamond?  Only relevant for
    // duration-mode bars — in plain mode the chord slot fills the bar
    // minus top_pad, with no diamond-dependent height.
    bool bar_has_diamond = false;
    for (const auto& ch : bar.chords())
        if (ch.is_diamond()) { bar_has_diamond = true; break; }

    qreal chord_slot_top = line_duration_mode
                           ? rect.top()
                           : rect.top() + top_pad;
    qreal chord_slot_h   = line_duration_mode
                           ? num_h + art_h + (bar_has_diamond
                                 ? chord_renderer::diamond_reserve_v(fonts, num_h) + 1.0 : 0.0)
                           : rect.height() - top_pad;

    qreal top_offset = line_has_articulation ? art_h : k_plain_top_pad;

    qreal num_top = chord_slot_top + top_offset;
    qreal num_h_actual = chord_slot_h - top_offset;
    return num_top + num_h_actual / 2.0;
}

// ---------------------------------------------------------------------------
// Public: paint
// ---------------------------------------------------------------------------
void bar_renderer::paint(QPainter& painter,
                         const QRectF& rect,
                         const model::bar& bar,
                         const chord_renderer::Fonts& fonts,
                         bool line_duration_mode,
                         bool line_has_articulation,
                         bool draw_begin_repeat,
                         bool draw_end_repeat,
                         bool draw_beat_parens,
                         qreal modulation_slot_w,
                         const model::time_signature& effective_time_sig)
{
    if (bar.empty())
        return;

    bool duration_mode = is_duration_mode(bar);

    // Beat-safe note splitting (see expand_rhythm_units) happens before
    // beaming, and beaming runs over the expanded sequence rather than the
    // raw chord list — a note that gets split can produce a new
    // eighth/sixteenth piece that's beam-eligible and adjacent to a real
    // neighbouring note (the classic case: an eighth followed by what
    // would be an off-beat dotted quarter splits into an eighth tied to a
    // quarter, and that new eighth piece beams with the first eighth
    // exactly as two ordinary adjacent eighths would).
    std::vector<rhythm_unit> units;
    std::vector<model::chord> unit_chords;
    std::vector<beam_group> beam_groups;
    std::vector<int> beam_group_of;  // unit index -> beam_groups index, or -1
    if (duration_mode)
    {
        const unsigned beat_16ths = compute_beat_16ths(effective_time_sig);
        units = expand_rhythm_units(bar.chords(), beat_16ths);

        unit_chords.reserve(units.size());
        for (const auto& u : units)
            unit_chords.push_back(u.piece);

        beam_groups = compute_beam_groups(unit_chords, effective_time_sig, bar.number_of_beats());
        beam_group_of.assign(units.size(), -1);
        for (std::size_t g = 0; g < beam_groups.size(); ++g)
            for (std::size_t i = beam_groups[g].start; i <= beam_groups[g].end; ++i)
                beam_group_of[i] = static_cast<int>(g);
    }

    constexpr qreal top_pad = 2.0;  // fixed top pad, independent of line height

    // For duration-mode lines the chord slot must be tall enough to hold the
    // number glyph plus the articulation zone (if any).  Using a fixed ratio of
    // the bar height fails once art height is added to the bar.  Compute it
    // directly from font metrics instead so it is always exact.
    QFontMetricsF nmFm_slot(fonts.number);
    QFontMetricsF artFm_slot(fonts.articulation);
    qreal num_h = nmFm_slot.ascent() + nmFm_slot.descent();
    qreal art_h = line_has_articulation ? artFm_slot.ascent() + artFm_slot.descent() : 0.0;

    // Does any chord on this line have a diamond? If so the diamond extends
    // below the tight glyph rect by diamond_reserve_v — the scaled padding
    // plus whatever half-height the containment growth adds.  Add that plus
    // 1px clearance so the rule always clears the diamond, including on wide
    // symbols where the diamond has grown to enclose the extensions.
    bool line_has_diamond = false;
    for (const auto& ch : bar.chords())
        if (ch.is_diamond()) { line_has_diamond = true; break; }

    qreal chord_slot_h  = line_duration_mode
                         ? num_h + art_h + (line_has_diamond
                               ? chord_renderer::diamond_reserve_v(fonts, num_h) + 1.0 : 0.0)
                         : rect.height() - top_pad;
    constexpr qreal k_rhythm_row_px = 16.0;  // must match duration_bar_height
    qreal rhythm_row_h = line_duration_mode ? k_rhythm_row_px : rect.height() * k_rhythm_row_ratio;

    // --- Layout: reserve slots for repeat marks first ---
    // The repeat-mark slots sit at the bar's outer edges, OUTSIDE the
    // time-signature slot's left position — i.e. the begin-repeat is
    // the leftmost element of the bar, the end-repeat the rightmost.
    // Reserving them up-front keeps the chord-row math independent of
    // whether repeat marks are drawn (they just shrink the available
    // chord-row width on the affected sides).
    qreal begin_repeat_left = rect.left();
    qreal end_repeat_right  = rect.right();
    qreal interior_left  = rect.left()  + (draw_begin_repeat ? k_repeat_slot_w : 0.0);
    qreal interior_right = rect.right() - (draw_end_repeat   ? k_repeat_slot_w : 0.0);

    // --- Time signature: always reserve k_time_sig_slot_w so chord columns align ---
    // Paint glyphs only when this bar actually has a time sig change.
    qreal chords_left = interior_left + k_time_sig_slot_w;

    if (bar.time_sig())
    {
        const auto& ts = *bar.time_sig();
        QFont tsFont = fonts.modifier;
        QFontMetricsF fm(tsFont);

        QString countStr = QString::number(ts.count());
        QString kindStr  = QString::number(static_cast<int>(ts.kind()));
        qreal max_adv = std::max(fm.horizontalAdvance(countStr),
                                fm.horizontalAdvance(kindStr));
        qreal ts_x    = interior_left + (k_time_sig_slot_w - max_adv) / 2.0;  // centred in slot
        qreal ts_y    = rect.top() + top_pad;
        qreal sep     = 3.0;

        // Numerator baseline = ts_y + ascent
        painter.save();
        painter.setFont(tsFont);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawText(QPointF(ts_x + (max_adv - fm.horizontalAdvance(countStr)) / 2.0,
                                 ts_y + fm.ascent()),
                         countStr);
        painter.restore();

        // Separator: sits sep px below the bottom of the numerator glyph
        qreal line_y = std::round(ts_y + fm.ascent() + fm.descent() + sep);
        painter.fillRect(QRectF(ts_x, line_y, max_adv, 1.0), Qt::black);

        // Denominator baseline = line_y + sep + ascent
        painter.save();
        painter.setFont(tsFont);
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawText(QPointF(ts_x + (max_adv - fm.horizontalAdvance(kindStr)) / 2.0,
                                 line_y + sep + fm.ascent()),
                         kindStr);
        painter.restore();
    }

    // --- Modulation indicator ---
    // The modulation slot is reserved COLUMN-WIDE (the value passed in by
    // the layout is the max over every bar in this column), so the chords
    // shift right by the same amount on every line — modulating or not —
    // keeping the chord columns aligned across lines.  We always advance
    // chords_left by the reserved width; we only paint a circle when THIS
    // bar actually starts a modulation.  The circle is drawn at the slot's
    // left edge so it sits just after the time-sig slot, with the chords
    // following the full reserved width.
    if (modulation_slot_w > 0.0)
    {
        if (!bar.modulation().empty())
            paint_modulation(painter, rect, chords_left, bar, fonts);
        chords_left += modulation_slot_w;
    }

    QRectF chord_slot_rect(chords_left,
                          line_duration_mode ? rect.top() : rect.top() + top_pad,
                          interior_right - chords_left,
                          chord_slot_h);

    qreal line_y = chord_slot_rect.bottom();

    // Compute per-chord widths
    std::vector<qreal> chord_widths;
    qreal total_chord_width = 0.0;
    for (const auto& ch : bar.chords())
    {
        qreal w = chord_renderer::size_hint(ch, fonts).width();
        chord_widths.push_back(w);
        total_chord_width += w;
    }

    qreal available_width = chord_slot_rect.width()
                           - k_inter_chord_spacing * (bar.chords().size() - 1);
    qreal scale = (total_chord_width > 0.0) ? available_width / total_chord_width : 1.0;

    // A beat-parens bar never stretches to fill extra column width — only
    // shrinks if its own content genuinely doesn't fit (scale < 1, kept
    // as the existing safety behaviour).  Ordinary bars DO stretch, which
    // is intentional column-filling; but for a beat-parens bar, letting
    // the parens hug their content tightly and always sit at a fixed
    // offset from chords_left is what keeps their position predictable
    // regardless of how wide the shared alignment column ends up being
    // (padding, or a wider bar sharing the same column on another line).
    // That predictability is what makes rect.right() usable again as the
    // continuation-dot anchor for these bars too — anything fancier here
    // broke vertical alignment between extended lines' dots, since
    // rect.right() (not any bar-specific content position) is the one
    // value guaranteed identical across every line sharing a column.
    if (draw_beat_parens)
        scale = std::min(scale, 1.0);

    qreal x = chords_left;
    qreal last_chord_right = chords_left;   // unscaled, used for multi-chord underline
    qreal last_slot_right  = chords_left;   // scaled slot right edge, used for ')'
    std::vector<chord_renderer::StemInfo> stems(units.size());
    std::size_t unit_idx = 0;
    const qreal rhythm_row_bottom = line_y + 1.0 + rhythm_row_h;
    for (std::size_t i = 0; i < bar.chords().size(); ++i)
    {
        qreal slot_width = chord_widths[i] * scale;
        QRectF slot_rect(x, chord_slot_rect.top(), slot_width, chord_slot_h);

        // Pass line_has_articulation normally — chord_renderer carves the art
        // zone from the top of the (now correctly sized) chord slot. This is
        // unaffected by beat-safe splitting: the chord number/articulations
        // are properties of the original chord and are drawn once regardless
        // of how many rhythm-row pieces it expands into below.
        chord_renderer::paint(painter, slot_rect, bar.chords()[i], fonts, duration_mode,
                              line_has_articulation);

        last_chord_right = x + chord_widths[i];  // actual glyph right edge (unscaled)
        last_slot_right  = x + slot_width;        // scaled slot right edge

        if (duration_mode && line_duration_mode)
        {
            // This chord's pieces are contiguous in `units` (expand_rhythm_units
            // preserves chord order and only ever grows one chord into several
            // consecutive entries), so a single scan forward finds its range.
            std::size_t first_unit = unit_idx;
            std::size_t last_unit  = unit_idx;
            while (last_unit < units.size() && units[last_unit].chord_index == i)
                ++last_unit;

            if (last_unit - first_unit <= 1)
            {
                // Common case: this chord needed no beat-safe splitting.
                QRectF rhythm_rect(x, line_y + 1.0, slot_width, rhythm_row_h);
                const bool beamed = beam_group_of[first_unit] >= 0;
                stems[first_unit] = chord_renderer::paint_rhythm(painter, rhythm_rect,
                                                                 units[first_unit].piece,
                                                                 fonts, beamed);
            }
            else
            {
                // Split into multiple tied pieces — divide this chord's own
                // slot width across them in proportion to each piece's share
                // of the total duration, so e.g. an eighth-tied-to-quarter
                // gives the quarter twice the eighth's width.
                unsigned total_16ths = 0;
                for (std::size_t u = first_unit; u < last_unit; ++u)
                    total_16ths += duration_in_16ths(*units[u].piece.duration());

                qreal piece_x = x;
                for (std::size_t u = first_unit; u < last_unit; ++u)
                {
                    const unsigned len = duration_in_16ths(*units[u].piece.duration());
                    const qreal piece_w = slot_width
                                        * (static_cast<qreal>(len) / total_16ths);
                    QRectF piece_rect(piece_x, line_y + 1.0, piece_w, rhythm_row_h);
                    const bool beamed = beam_group_of[u] >= 0;
                    stems[u] = chord_renderer::paint_rhythm(painter, piece_rect,
                                                            units[u].piece, fonts, beamed);
                    piece_x += piece_w;
                }
            }

            unit_idx = last_unit;
        }

        x += slot_width + k_inter_chord_spacing;
    }

    // --- Beams ---
    // Drawn as a pass separate from the per-chord loop above because a beam
    // is shared geometry spanning several chord slots, not a property of
    // any single one. Operates on the expanded unit sequence (unit_chords),
    // not the raw chord list — see the comment where units/beam_groups are
    // computed for why a split piece needs to be beam-eligible too.
    if (duration_mode && line_duration_mode)
    {
        for (const beam_group& group : beam_groups)
            paint_beam(painter, unit_chords, stems, group);

        // Tie arcs between pieces of the same original note (beat-safe
        // splitting) — NOT the pre-existing chord-level is_tied() arc,
        // which is a different indicator for a tie between two distinct
        // chords and is left completely alone here.
        for (std::size_t k = 0; k + 1 < units.size(); ++k)
        {
            if (units[k].tie_to_next
                && units[k].chord_index == units[k + 1].chord_index
                && stems[k].has_stem && stems[k + 1].has_stem)
            {
                paint_split_tie(painter, stems[k].stem_x, stems[k + 1].stem_x,
                                rhythm_row_bottom);
            }
        }
    }

    // Single underline from chords-left to just past the last chord glyph
    if (bar.chords().size() > 1)
    {
        constexpr qreal k_underline_overhang = 4.0;
        painter.fillRect(QRectF(chords_left,
                                line_y,
                                last_chord_right + k_underline_overhang - chords_left,
                                1.0),
                         Qt::black);
    }

    // Bar-wide horizontal rule for duration-mode bars — same weight as the
    // multi-chord underline so they appear visually identical.
    if (duration_mode && line_duration_mode)
    {
        // Identical to the multi-chord underline: crisp 1px black fillRect.
        painter.fillRect(QRectF(chords_left, line_y,
                                chord_slot_rect.width(), 1.0),
                         Qt::black);
    }

    // Parentheses around chord numbers for bars with a custom beat count.
    // A opening '(' sits just to the left of the first chord, a closing ')'
    // just to the right of the last, both vertically centred on the number
    // row so they read as belonging to the chord symbols rather than to the
    // articulation zone or the rhythm row.
    if (draw_beat_parens && !bar.chords().empty())
    {
        painter.save();
        QFont pf = fonts.number;
        painter.setFont(pf);
        QFontMetricsF pfm(pf);

        // Baseline: vertically centres the paren ink on the number row.
        // The number row sits below the articulation zone (or top_pad when
        // there is none), mirroring chord_renderer::paint_number_row.
        const qreal paren_baseline = line_duration_mode
            ? chord_slot_rect.top() + art_h + pfm.ascent()
            : rect.top() + top_pad + (line_has_articulation ? art_h : 4.0) + pfm.ascent();

        const qreal paren_w = pfm.horizontalAdvance("(");
        painter.setPen(QPen(Qt::black, 1.0));
        painter.drawText(QPointF(chords_left - paren_w - 2.0, paren_baseline), "(");
        painter.drawText(QPointF(last_chord_right + 2.0,      paren_baseline), ")");
        painter.restore();
    }

    // --- Repeat marks ---
    // Painted last so they sit on top of any rule/underline strokes.
    //
    // Vertically the mark spans just the chord-number glyph height,
    // centred on the number row.  Articulations above the row (push
    // arrow, staccato dot, tied-into arc) and anything below the row
    // (split-bar underline, duration-mode rhythm row) are explicitly
    // OUTSIDE the visual scope of the repeat — those elements belong
    // to individual chords, not to the bar's repeat boundary.  The
    // wings (drawn by paint_repeat_mark) flare outward from the bar
    // ends but stay close enough that they don't visually invade the
    // articulation row either.
    if (draw_begin_repeat || draw_end_repeat)
    {
        QFontMetricsF nmFm(fonts.number);
        qreal num_h_repeat  = nmFm.ascent() + nmFm.descent();
        qreal centre_y      = number_row_center_y(rect, bar, fonts,
                                                  line_duration_mode,
                                                  line_has_articulation);
        qreal mark_top      = centre_y - num_h_repeat / 2.0;
        qreal mark_bottom   = centre_y + num_h_repeat / 2.0;
        qreal mark_h        = mark_bottom - mark_top;

        if (draw_begin_repeat)
        {
            QRectF mark_rect(begin_repeat_left, mark_top,
                             k_repeat_slot_w, mark_h);
            paint_repeat_mark(painter, mark_rect, /*is_begin=*/true);
        }
        if (draw_end_repeat)
        {
            QRectF mark_rect(end_repeat_right - k_repeat_slot_w, mark_top,
                             k_repeat_slot_w, mark_h);
            paint_repeat_mark(painter, mark_rect, /*is_begin=*/false);
        }
    }
}

// ---------------------------------------------------------------------------
// Private: paint_repeat_mark
// ---------------------------------------------------------------------------
// Nashville charts have no staff to anchor the conventional barline-dot-
// dot pattern, so the dots' vertical position can read as floating.  The
// little angled "wings" hooking off the thick bar fill that gap — they
// signal the boundaries of the repeat the way the top and bottom staff
// lines would in conventional notation.  Each wing flares OUTWARD from
// the bar end: the top wing rises up-and-out (above the bar's top), the
// bottom wing falls down-and-out (below the bar's bottom).  The wings
// flare in the same direction as the dots (toward the music being
// repeated).
//
// NOTE: The wings still have small join issues with the thick bar at
// their anchor point and the bar may show a sliver of itself outside
// the wing.  This is a known visual issue to be revisited.  The
// previous polygon-based approach (with wings as integrated chamfers
// of the bar) eliminated the join issue but produced a different
// visual that was rejected as a regression.
//
// Geometry (begin-repeat, mirrored for end):
//
//          /  ┃ ┃          ▲ top wing: rises up-and-right, away from
//         /   ┃ ┃            the bar's top end
//             ┃ ┃ thin
//             ┃ ┃│ • (dot)
//        thick┃ ┃│
//             ┃ ┃│ • (dot)
//             ┃ ┃
//         \   ┃ ┃          ▼ bottom wing: falls down-and-right, away
//          \  ┃ ┃            from the bar's bottom end
//
void bar_renderer::paint_repeat_mark(QPainter& painter,
                                     const QRectF& mark_rect,
                                     bool is_begin)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Centre the two-bar block horizontally in the slot.
    qreal bars_total_w = k_thick_w + k_bars_gap + k_thin_w;
    qreal block_left   = mark_rect.left() + (mark_rect.width() - bars_total_w) / 2.0;

    qreal thick_x, thin_x;
    if (is_begin)
    {
        // Begin-repeat: thick bar to the left of the thin bar, dots and
        // wings face into the music (to the right).
        thick_x = block_left;
        thin_x  = block_left + k_thick_w + k_bars_gap;
    }
    else
    {
        // End-repeat: mirror image — thin bar to the left of the thick.
        thin_x  = block_left;
        thick_x = block_left + k_thin_w + k_bars_gap;
    }

    qreal top    = mark_rect.top();
    qreal bottom = mark_rect.bottom();

    // --- Wings (painted BEFORE the bars) ---
    // The clean-join problem: a stroked diagonal line meeting the
    // corner of a filled rectangle inevitably leaves a tiny visual
    // seam — either a step at the bar's corner (if the stroke's
    // outer edge starts at the corner but the stroke itself sits
    // beside the bar), or a kink (if the stroke crosses the bar's
    // edge at the rasteriser-centred mid-line).
    //
    // We resolve it by painting the wings first, with the path
    // anchored slightly INSIDE the bar's outline.  The bar's filled
    // rect is then painted on top and crisply masks the portion of
    // the wing stroke that lies within the bar — so the visible part
    // of each wing is exactly its outer extent, emerging cleanly from
    // the bar's edge.
    //
    // Path geometry: the stroke's OUTER edge passes through the bar's
    // corner point (otherwise the bar's corner sticks up beyond where
    // the wing emerges).  Achieved by computing a perpendicular
    // displacement (towards the centre of the repeat mark) of
    // magnitude k_half_pen, then extending the path tail backward
    // along the wing's inverse outward direction so the stroke
    // definitely overlaps into the bar's body for masking.
    //
    // The perpendicular always points (a) along the bar edge the
    // wing emerges from (so for begin: positive X; for end: negative
    // X) and (b) towards the mark's vertical centre (so for top:
    // positive Y; for bottom: negative Y).  Its components are
    //   perp_x = x_sign * |outward_y|
    //   perp_y = y_sign * |outward_x|
    // — a 90° rotation of outward, with the rotation direction
    // chosen so the resulting vector points into the mark's body.
    constexpr qreal k_wing_pen_w   = 1.2;
    constexpr qreal k_half_pen     = k_wing_pen_w / 2.0;
    // Extend the wing's tail INTO the bar by this much (measured along
    // the wing direction) so the bar's fill, painted afterward, has a
    // generous overlap to mask.
    constexpr qreal k_overlap_into_bar = 1.0;

    const qreal L          = std::sqrt(k_wing_len * k_wing_len
                                       + k_wing_dy * k_wing_dy);
    const qreal unit_dx    = k_wing_len / L;    // unit outward X magnitude
    const qreal unit_dy    = k_wing_dy  / L;    // unit outward Y magnitude

    QPen wing_pen(Qt::black, k_wing_pen_w);
    wing_pen.setCapStyle(Qt::FlatCap);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(wing_pen);

    // x_sign: which side of the bar the wing emerges from.  For
    //   begin (outward = right): +1.  For end (outward = left): -1.
    const qreal x_sign      = is_begin ? +1.0 : -1.0;
    const qreal bar_edge_x  = is_begin ? (thick_x + k_thick_w) : thick_x;

    // y_sign distinguishes top wing (rises up) from bottom wing
    // (falls down).
    auto paint_wing = [&](qreal corner_y, qreal y_sign) {
        const qreal ox     = x_sign * unit_dx;          // outward X (signed)
        const qreal oy     = -y_sign * unit_dy;         // outward Y (signed)
        const qreal perp_x = x_sign * unit_dy;          // perp X (signed)
        const qreal perp_y = y_sign * unit_dx;          // perp Y (signed)

        const qreal anchor_x = bar_edge_x
                              + perp_x * k_half_pen
                              - ox      * k_overlap_into_bar;
        const qreal anchor_y = corner_y
                              + perp_y * k_half_pen
                              - oy      * k_overlap_into_bar;

        const qreal tip_outer_x = bar_edge_x + ox * k_wing_len;
        const qreal tip_outer_y = corner_y   + oy * k_wing_len;
        const qreal tip_x       = tip_outer_x + perp_x * k_half_pen;
        const qreal tip_y       = tip_outer_y + perp_y * k_half_pen;

        QPainterPath wp;
        wp.moveTo(anchor_x, anchor_y);
        wp.lineTo(tip_x,    tip_y);
        painter.drawPath(wp);
    };

    paint_wing(top,    +1.0);   // top wing: rises (outward Y negative)
    paint_wing(bottom, -1.0);   // bottom wing: falls (outward Y positive)

    // --- Bars (painted AFTER wings to mask their inside portion) ---
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);
    painter.drawRect(QRectF(thick_x, top, k_thick_w, bottom - top));
    painter.drawRect(QRectF(thin_x,  top, k_thin_w,  bottom - top));

    // --- Dots ---
    // Vertically split around the mark's centre.  The dot column
    // sits between the two bars, biased toward the thin-bar side so
    // the dots aren't visually crowded against the thick bar.
    qreal centre_y = (top + bottom) / 2.0;
    qreal dot_x;
    if (is_begin)
        dot_x = thick_x + k_thick_w + k_bars_gap + k_thin_w + k_dot_inset;
    else
        dot_x = thick_x - k_bars_gap - k_thin_w - k_dot_inset;
    qreal dot_dy = std::max(4.0, (bottom - top) / 6.0);
    painter.drawEllipse(QPointF(dot_x, centre_y - dot_dy), k_dot_r, k_dot_r);
    painter.drawEllipse(QPointF(dot_x, centre_y + dot_dy), k_dot_r, k_dot_r);

    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_modulation
// ---------------------------------------------------------------------------
// Mirrors the song-margin key circle (margin_renderer): a thin black ellipse
// with the label centred inside, the accidental left as-is (modulation keys
// are short names like "Bb" / "F#m" and read fine at this size).  Sized down
// via compute_mod_layout so the circle clears the bar's vertical limits.
void bar_renderer::paint_modulation(QPainter& painter,
                                    const QRectF& bar_rect,
                                    qreal slot_left,
                                    const model::bar& bar,
                                    const chord_renderer::Fonts& fonts)
{
    mod_layout ml = compute_mod_layout(bar, bar_rect.height(), fonts);
    if (!ml.present)
        return;

    // Circle centred horizontally in its slot (the circle occupies the slot
    // minus the trailing chord gap) and vertically in the full bar height.
    const QPointF center(slot_left + ml.radius, bar_rect.center().y());

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Same look as the margin key circle, just a thinner stroke to stay in
    // proportion with the smaller radius.
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 1.2));
    painter.drawEllipse(center, ml.radius, ml.radius);

    QFontMetricsF fm(ml.font);
    painter.setFont(ml.font);
    painter.setPen(QPen(Qt::black, 1.0));
    const qreal text_x   = center.x() - ml.text_w / 2.0;
    const qreal baseline = center.y() + (fm.ascent() - fm.descent()) / 2.0;
    painter.drawText(QPointF(text_x, baseline), ml.text);

    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: is_duration_mode
// ---------------------------------------------------------------------------
bool bar_renderer::is_duration_mode(const model::bar& bar)
{
    if (bar.empty())
        return false;
    // A duration anywhere, or any rest, puts the bar in rhythm mode.  The UI
    // keeps bars duration-uniform, but a rest with no explicit duration must
    // still drop below the rule (as a whole rest), so we scan every chord
    // rather than trusting the first one alone.
    for (const auto& ch : bar.chords())
        if (ch.duration().has_value() || ch.is_rest())
            return true;
    return false;
}

// ---------------------------------------------------------------------------
// Private: duration_in_16ths — helper local to compute_beam_groups
// ---------------------------------------------------------------------------
// The chord::time enum's underlying integer values are not the notes'
// actual relative lengths (DOTTED_EIGHTH = 12 is not "1.5x" anything else
// in that encoding) — they look intended for sorting/display, not beat
// arithmetic. This is the real duration, in sixteenth-note units, needed
// to track position through a bar.
static unsigned duration_in_16ths(model::chord::time t)
{
    switch (t)
    {
        case model::chord::time::WHOLE:          return 16;
        case model::chord::time::DOTTED_HALF:    return 12;
        case model::chord::time::HALF:           return 8;
        case model::chord::time::DOTTED_QUARTER: return 6;
        case model::chord::time::QUARTER:        return 4;
        case model::chord::time::DOTTED_EIGHTH:  return 3;
        case model::chord::time::EIGHTH:         return 2;
        case model::chord::time::SIXTEENTH:      return 1;
    }
    return 4;  // unreachable; keeps position tracking sane if the enum grows
}

// A note is a beam *candidate* purely by virtue of its own duration —
// whether it ends up in an actual multi-note beam_group also depends on
// its neighbours, decided below in compute_beam_groups.
static bool is_beam_candidate(const model::chord& ch)
{
    if (ch.is_rest() || !ch.duration())
        return false;
    switch (*ch.duration())
    {
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:
        case model::chord::time::SIXTEENTH:
            return true;
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Private: compute_beat_16ths
// ---------------------------------------------------------------------------
unsigned bar_renderer::compute_beat_16ths(const model::time_signature& ts)
{
    const bool is_compound = ts.kind() == model::time_signature::beat_type::EIGHTH
                            && ts.count() % 3 == 0
                            && ts.count() >= 6;
    if (is_compound)
        return 6;  // dotted quarter = 3 eighths

    switch (ts.kind())
    {
        case model::time_signature::beat_type::HALF:   return 8;
        case model::time_signature::beat_type::EIGHTH: return 2;
        case model::time_signature::beat_type::QUARTER:
        default:                                        return 4;
    }
}

// ---------------------------------------------------------------------------
// Private: compute_beam_groups
// ---------------------------------------------------------------------------
std::vector<bar_renderer::beam_group> bar_renderer::compute_beam_groups(
    const std::vector<model::chord>& chords,
    const model::time_signature& ts,
    const std::optional<unsigned>& beat_count_override)
{
    // --- Beat structure implied by the time signature ---
    const bool is_compound = ts.kind() == model::time_signature::beat_type::EIGHTH
                            && ts.count() % 3 == 0
                            && ts.count() >= 6;

    const unsigned beat_16ths = compute_beat_16ths(ts);
    unsigned beat_count = is_compound ? ts.count() / 3 : ts.count();

    // A bar with a custom beat count (a pickup bar, say) has a different
    // number of beats than the time signature's nominal count — and
    // therefore a different mid-bar point — even though beat_16ths (how
    // long one beat lasts) is unchanged.
    if (beat_count_override)
        beat_count = *beat_count_override;

    // Only simple meters (quarter/half beat) with an even beat count get
    // the eighth-note merge-across-a-beat-boundary pass; see the rationale
    // in the header comment on compute_beam_groups.
    const bool allow_eighth_merge = !is_compound
                                   && ts.kind() != model::time_signature::beat_type::EIGHTH
                                   && beat_count % 2 == 0;

    // The beat index where the second half of the bar begins. A merge is
    // only ever allowed to bring two adjacent beat-runs together when they
    // both fall on the same side of this line — this is the actual rule
    // ("a beam must never cross a mid-bar boundary"), checked directly by
    // position rather than by the beat_index parity trick this used to
    // rely on. Parity only happens to match the true midpoint when
    // beat_count is a multiple of 4 (as in 4/4: midpoint at beat 2, which
    // is even); for other even beat counts — 6/4, say, midpoint at beat 3
    // — a parity check would happily merge beats 2 and 3 straight across
    // the middle. This check is correct for every even beat_count.
    const unsigned half_boundary = beat_count / 2;

    // --- Pass 1: one run per beat (or fragment of a beat, if a rest splits it) ---
    struct beat_run
    {
        std::size_t start;
        std::size_t end;          // inclusive
        unsigned    beat_index;
        bool        has_sixteenth;
    };
    std::vector<beat_run> runs;

    unsigned position_16ths = 0;
    bool     in_run         = false;
    beat_run current{};

    auto close_run = [&]()
    {
        if (in_run)
            runs.push_back(current);
        in_run = false;
    };

    for (std::size_t i = 0; i < chords.size(); ++i)
    {
        const model::chord& ch = chords[i];
        const unsigned beat_index = position_16ths / beat_16ths;

        if (is_beam_candidate(ch))
        {
            const bool is_sixteenth = (*ch.duration() == model::chord::time::SIXTEENTH);
            if (in_run && current.beat_index == beat_index && current.end + 1 == i)
            {
                current.end = i;
                current.has_sixteenth = current.has_sixteenth || is_sixteenth;
            }
            else
            {
                close_run();
                current = beat_run{i, i, beat_index, is_sixteenth};
                in_run  = true;
            }
        }
        else
        {
            close_run();
        }

        // Advance position by this chord's duration. A chord with no
        // duration (and not a rest) contributes nothing — nothing is
        // painted for it in the rhythm row either (see paint_rhythm),
        // so it can't shift where later notes land.
        if (ch.is_rest())
            position_16ths += ch.duration() ? duration_in_16ths(*ch.duration()) : 16;
        else if (ch.duration())
            position_16ths += duration_in_16ths(*ch.duration());
    }
    close_run();

    // --- Pass 2: merge adjacent pure-eighth runs, never across half_boundary ---
    std::vector<beam_group> groups;
    std::size_t r = 0;
    while (r < runs.size())
    {
        beat_run merged = runs[r];
        if (allow_eighth_merge && !merged.has_sixteenth
            && r + 1 < runs.size())
        {
            const beat_run& next = runs[r + 1];
            const bool would_cross_midbar = merged.beat_index < half_boundary
                                           && next.beat_index >= half_boundary;
            if (!next.has_sixteenth
                && next.beat_index == merged.beat_index + 1
                && next.start == merged.end + 1
                && !would_cross_midbar)
            {
                merged.end = next.end;
                ++r;  // consumed the next run too
            }
        }

        if (merged.end > merged.start)  // only actual multi-note groups
            groups.push_back(beam_group{merged.start, merged.end});

        ++r;
    }

    return groups;
}

// ---------------------------------------------------------------------------
// Private: paint_beam
// ---------------------------------------------------------------------------
void bar_renderer::paint_beam(QPainter& painter,
                              const std::vector<model::chord>& chords,
                              const std::vector<chord_renderer::StemInfo>& stems,
                              const beam_group& group)
{
    if (group.end <= group.start || !stems[group.start].has_stem)
        return;

    // Snap everything to whole pixels and turn antialiasing off for these
    // fills: beams here are always plain axis-aligned rectangles, and
    // antialiasing a rect whose edge lands within rounding error of a
    // pixel boundary can leave that edge only partially covered — visible
    // as a missing corner pixel where the beam meets the stem. A hard,
    // integer-aligned fill has no such edge case.
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::black);

    const qreal notehead_h = stems[group.start].notehead_h;
    // beam_y is the top of every note's stem in this group (identical for
    // all of them — same rhythm row) — i.e. exactly where each note's
    // flag used to start. The primary beam's TOP edge belongs there too,
    // extending downward, so the stem visually runs straight into the
    // beam with no gap or overlap. Centering the beam on beam_y instead
    // (an earlier version of this code did) leaves half the beam's
    // thickness above the point the stem actually reaches — nothing
    // underneath it, reading as misaligned.
    const qreal beam_y    = std::round(stems[group.start].beam_y);
    const qreal thickness = std::round(std::max(1.0, notehead_h * 0.20));
    const qreal gap       = std::round(std::max(1.0, notehead_h * 0.30));

    // Beam level for a chord: 2 for a sixteenth (gets a second beam), 1
    // for anything else beam-eligible (eighth/dotted-eighth — the dot
    // doesn't add a beam, just the augmentation dot chord_renderer already
    // draws). Chords land in a beam_group only when eligible, so no 0 case
    // is expected here, but a rest/undurationed chord defensively reads 0.
    auto beam_level = [](const model::chord& ch) -> int
    {
        if (!ch.duration())
            return 0;
        return (*ch.duration() == model::chord::time::SIXTEENTH) ? 2 : 1;
    };

    const qreal x_start = std::round(stems[group.start].stem_x);
    const qreal x_end   = std::round(stems[group.end].stem_x);

    // Primary beam: one solid bar spanning the full group, top-aligned to
    // the stem tip and extending down toward the noteheads.
    painter.drawRect(QRectF(x_start, beam_y, x_end - x_start, thickness));

    // Secondary (sixteenth) beam, stacked directly below the primary with
    // a small visible gap, one adjacent pair at a time. A full segment
    // when both notes are sixteenths; otherwise a short partial stub
    // reaching from the sixteenth note toward its non-sixteenth
    // neighbour, the standard way of notating e.g. a dotted-eighth
    // followed by a sixteenth within one beamed group.
    const qreal secondary_y    = beam_y + thickness + gap;
    constexpr qreal k_stub_max = 6.0;
    for (std::size_t i = group.start; i < group.end; ++i)
    {
        const int   lvl_a = beam_level(chords[i]);
        const int   lvl_b = beam_level(chords[i + 1]);
        const qreal xa    = std::round(stems[i].stem_x);
        const qreal xb    = std::round(stems[i + 1].stem_x);

        if (lvl_a >= 2 && lvl_b >= 2)
        {
            painter.drawRect(QRectF(xa, secondary_y, xb - xa, thickness));
        }
        else if (lvl_a >= 2)
        {
            const qreal stub = std::round(std::min(k_stub_max, (xb - xa) * 0.5));
            painter.drawRect(QRectF(xa, secondary_y, stub, thickness));
        }
        else if (lvl_b >= 2)
        {
            const qreal stub = std::round(std::min(k_stub_max, (xb - xa) * 0.5));
            painter.drawRect(QRectF(xb - stub, secondary_y, stub, thickness));
        }
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: value_for_16ths — inverse of duration_in_16ths
// ---------------------------------------------------------------------------
static model::chord::time value_for_16ths(unsigned len)
{
    switch (len)
    {
        case 16: return model::chord::time::WHOLE;
        case 12: return model::chord::time::DOTTED_HALF;
        case 8:  return model::chord::time::HALF;
        case 6:  return model::chord::time::DOTTED_QUARTER;
        case 4:  return model::chord::time::QUARTER;
        case 3:  return model::chord::time::DOTTED_EIGHTH;
        case 2:  return model::chord::time::EIGHTH;
        case 1:
        default: return model::chord::time::SIXTEENTH;
    }
}

// ---------------------------------------------------------------------------
// Private: decompose_into_valid_values
// ---------------------------------------------------------------------------
// Greedy largest-fits-first decomposition of a length (in sixteenth-note
// units) into a sequence of this app's representable note values. Only
// needed when a length isn't itself one of those values outright — e.g. a
// 5-sixteenth remainder becomes [4, 1] (quarter tied to sixteenth). Always
// terminates: 1 (sixteenth) is in the value set, so worst case a length
// dissolves entirely into single sixteenths.
static std::vector<unsigned> decompose_into_valid_values(unsigned length_16ths)
{
    static constexpr unsigned k_valid_desc[] = {16, 12, 8, 6, 4, 3, 2, 1};
    std::vector<unsigned> result;
    while (length_16ths > 0)
    {
        for (unsigned v : k_valid_desc)
        {
            if (v <= length_16ths)
            {
                result.push_back(v);
                length_16ths -= v;
                break;
            }
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Private: split_note_across_beats
// ---------------------------------------------------------------------------
// A note starting exactly on a beat may run for any length, crossing as
// many further beat boundaries as it likes, with no splitting needed — a
// half note starting on beat 1 spanning into beat 2 is completely normal.
// The problem is only ever a note that starts OFF the beat and would then
// reach or cross the next beat boundary, obscuring where that beat starts:
// that gets split at the boundary, with everything from there on already
// beat-aligned and therefore needing no further boundary-driven split
// (though decompose_into_valid_values may still break a leftover length
// into more than one tied piece if it isn't a single representable value).
static std::vector<unsigned> split_note_across_beats(unsigned start, unsigned total,
                                                     unsigned beat_16ths)
{
    std::vector<unsigned> pieces;
    if (beat_16ths == 0)
    {
        return decompose_into_valid_values(total);  // defensive; shouldn't happen
    }

    if (start % beat_16ths != 0)
    {
        const unsigned next_boundary = ((start / beat_16ths) + 1) * beat_16ths;
        const unsigned gap  = next_boundary - start;
        const unsigned take = std::min(gap, total);
        for (unsigned v : decompose_into_valid_values(take))
            pieces.push_back(v);
        start += take;
        total -= take;
    }

    if (total > 0)
        for (unsigned v : decompose_into_valid_values(total))
            pieces.push_back(v);

    return pieces;
}

// ---------------------------------------------------------------------------
// Private: expand_rhythm_units
// ---------------------------------------------------------------------------
std::vector<bar_renderer::rhythm_unit> bar_renderer::expand_rhythm_units(
    const std::vector<model::chord>& chords,
    unsigned beat_16ths)
{
    std::vector<rhythm_unit> units;
    unsigned position_16ths = 0;

    for (std::size_t i = 0; i < chords.size(); ++i)
    {
        const model::chord& ch = chords[i];

        // Rests, and chords with no duration set, are never split — see
        // the header comment on expand_rhythm_units.
        if (ch.is_rest() || !ch.duration())
        {
            rhythm_unit u;
            u.chord_index = i;
            u.piece       = ch;
            u.first_piece = true;
            u.tie_to_next = false;
            units.push_back(u);
        }
        else
        {
            const unsigned total = duration_in_16ths(*ch.duration());
            std::vector<unsigned> piece_lengths =
                split_note_across_beats(position_16ths, total, beat_16ths);

            for (std::size_t p = 0; p < piece_lengths.size(); ++p)
            {
                rhythm_unit u;
                u.chord_index = i;
                u.piece       = model::chord();
                u.piece.number(1);  // any non-REST value; is_rest()/duration() are
                                     // all chord_renderer's rhythm-row painting reads
                u.piece.duration(value_for_16ths(piece_lengths[p]));
                u.first_piece = (p == 0);
                // Internal pieces (artifacts of the same original note) are
                // always tied to the next piece; the last piece carries the
                // original chord's own tie-to-the-next-CHORD flag, unchanged.
                u.tie_to_next = (p + 1 < piece_lengths.size()) ? true : ch.is_tied();
                units.push_back(u);
            }
        }

        if (ch.is_rest())
            position_16ths += ch.duration() ? duration_in_16ths(*ch.duration()) : 16;
        else if (ch.duration())
            position_16ths += duration_in_16ths(*ch.duration());
    }

    return units;
}

// ---------------------------------------------------------------------------
// Private: paint_split_tie
// ---------------------------------------------------------------------------
// A short tie arc between two rhythm-row noteheads that are pieces of the
// same original note (see expand_rhythm_units) — distinct from the
// existing chord-level is_tied() arc, which lives in the number row and
// indicates a tie between two different chords. This one arcs below the
// noteheads (opposite the stems, which point up), the conventional side.
void bar_renderer::paint_split_tie(QPainter& painter, qreal from_x, qreal to_x,
                                   qreal row_bottom)
{
    if (to_x <= from_x)
        return;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(Qt::black, 1.0));
    painter.setBrush(Qt::NoBrush);

    const qreal base_y = row_bottom + 1.0;
    const qreal bulge   = std::min(4.0, (to_x - from_x) * 0.3);

    QPainterPath path;
    path.moveTo(from_x, base_y);
    path.quadTo((from_x + to_x) / 2.0, base_y + bulge, to_x, base_y);
    painter.drawPath(path);

    painter.restore();
}

} // namespace nashville::view
