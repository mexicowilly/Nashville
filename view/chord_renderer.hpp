#pragma once

#include "../model/chord.hpp"
#include <QPainter>
#include <QRectF>
#include <QFont>

namespace nashville::view
{

// Stateless renderer for a single chord.
// All methods are static — instantiate only to configure fonts.
class chord_renderer
{
public:
    // Fonts should be set once by SongBodyWidget and passed in.
    struct Fonts
    {
        QFont number;        // dominant size — chord number
        QFont modifier;      // smaller — mode suffix, extensions, bass note, step
        QFont articulation;  // pushed '>', staccato dot sizing
        QFont music;         // Bravura or fallback — note glyphs in rhythmic row
    };

    // Vertical zone proportions within the full chord slot rect.
    // Public so SongBodyWidget can use them for height calculations.
    static constexpr qreal k_number_zone_ratio           = 0.65;
    static constexpr qreal k_articulation_zone_ratio     = 0.35;

    // Diamond padding around the tight number glyph rect, at k_number_ref_pt.
    // Public so bar_renderer can account for vertical clearance in slot sizing.
    // These are the *reference* values; the drawn diamond and every clearance
    // computed from it scale with the live number font (see diamond_padding_*
    // below), so use those helpers rather than these raw constants.
    static constexpr qreal k_diamond_padding_h = 10.0;  // left/right
    static constexpr qreal k_diamond_padding_v =  6.0;  // top/bottom

    // Reference number point size the absolute paddings above are tuned
    // against (matches song_body_widget::k_number_base_pt).  The diamond
    // padding is scaled by the live number font relative to this so the
    // diamond stays proportional to the number at any font scale.  Without
    // it, the fixed 6px overhang collides with the chord/diamond on the line
    // above or below at the small scales the horizontal auto-fit produces
    // (and looks undersized at large Text sizes).
    static constexpr qreal k_number_ref_pt = 15.0;

    // Diamond padding scaled to the live number font.  Use these — not the
    // raw k_diamond_padding_* constants — everywhere the drawn diamond's size
    // or its reserved vertical clearance is computed, so drawing and
    // reservation always agree.
    //
    // The scale is clamped to 1.0: the padding only ever *shrinks* below the
    // reference size, never grows.  Shrinking at small scales is what fixes
    // the collision (a fixed 6px overhang is too large once the auto-fit
    // shrinks the fonts).  Growing at large Text sizes would instead let the
    // diamond outgrow the fixed inter-line spacing, so we hold it at the
    // reference there — identical to the original fixed-padding behaviour.
    static qreal diamond_padding_h(const Fonts& fonts)
    {
        const qreal s = fonts.number.pointSizeF() / k_number_ref_pt;
        return k_diamond_padding_h * (s < 1.0 ? s : 1.0);
    }
    static qreal diamond_padding_v(const Fonts& fonts)
    {
        const qreal s = fonts.number.pointSizeF() / k_number_ref_pt;
        return k_diamond_padding_v * (s < 1.0 ? s : 1.0);
    }

    // How full of ink the diamond is allowed to be.  The diamond's edges are
    // the four lines joining the midpoints of its bounding rect, so the rect's
    // corners fall OUTSIDE the diamond: enclosing a content rect is not the
    // same as padding it.  For a content rect w x h inside a diamond with
    // half-axes a and b, the corner (w/2, h/2) is inside exactly when
    // (w/2)/a + (h/2)/b <= 1.  This constant is the target for that sum, so
    // there is a little clear space between the ink and the sloping edge.
    // Raise it towards 1.0 for a tighter diamond, lower it for a roomier one.
    static constexpr qreal k_diamond_corner_fill = 0.95;

    // Ceiling on how far diamond_bounds may grow the padded rect to achieve
    // containment.  Very wide symbols would otherwise demand an arbitrarily
    // large diamond; past this the diamond stops growing and the widest
    // extensions are allowed to graze the edge, which reads far better than a
    // diamond that dwarfs its own line.  bar_renderer reserves vertical
    // clearance for exactly this worst case (see diamond_reserve_v), so the
    // cap is what keeps drawing and reservation in agreement.
    static constexpr qreal k_diamond_max_growth = 1.6;

    // The bounding rect of the diamond drawn around `content`.  The diamond
    // itself is inscribed in the returned rect.  Use this anywhere the
    // diamond's extent matters rather than padding `content` directly, which
    // is what produced diamonds too short to contain wide symbols such as
    // 1maj7 while looking correct on narrow ones such as 6-.
    static QRectF diamond_bounds(const QRectF& content, const Fonts& fonts);

    // Worst-case distance the diamond reaches above (and below) the ink of a
    // chord whose ink is `ink_h` tall.  Growth is applied about the centre, so
    // the overhang is the scaled padding plus the half-height the scaling adds
    // — it is not simply diamond_padding_v.  Reserving the old value was why a
    // grown diamond could reach into the line above.
    static qreal diamond_reserve_v(const Fonts& fonts, qreal ink_h)
    {
        const qreal s = k_diamond_max_growth;
        return diamond_padding_v(fonts) * s + (ink_h * 0.5) * (s - 1.0);
    }

    // Distance from the TOP of the articulation zone down to the topmost ink
    // any articulation actually paints, for a zone art_h tall.  Articulations
    // are anchored to the BOTTOM of that zone — the staccato diamond sits just
    // above its floor, the push '>' uses the floor as its text baseline, and
    // the tie arc peaks at k_tied_top_ratio — so the upper part of the zone is
    // typically empty.  Anything placed above the articulations (the beat-dot
    // row) should measure against this rather than assume the ink begins at
    // the zone's top edge, which is what left a visible gap above the dots.
    //
    // has_both_on_one_chord reports whether any single chord is both staccato
    // and pushed; paint_articulations splits the zone in half for that case,
    // which lifts the staccato ink by half the zone and is the tightest
    // arrangement there is.
    static qreal articulation_headroom(const Fonts& fonts, qreal art_h,
                                       bool has_staccato, bool has_pushed,
                                       bool has_tied,
                                       bool has_both_on_one_chord);

    // Returns the minimum bounding size needed to render this chord,
    // given the supplied fonts. Used by the layout pass.
    static QSizeF size_hint(const model::chord& ch, const Fonts& fonts);

    // Paints the chord into rect on painter.
    // rect is the full chord slot (articulation zone + number row).
    // If is_duration_mode is true, the number row is the upper portion only;
    // the caller (BarRenderer) owns the rule and rhythmic row.
    static void paint(QPainter& painter,
                      const QRectF& rect,
                      const model::chord& ch,
                      const Fonts& fonts,
                      bool is_duration_mode,
                      bool line_has_articulation);

    // Geometry handed back from paint_rhythm so a caller doing beaming
    // (bar_renderer) can connect stems across notes without recomputing
    // this renderer's internal layout math. Only meaningful when has_stem
    // is true (whole notes and rests have no stem).
    struct StemInfo
    {
        bool  has_stem   = false;
        qreal stem_x     = 0.0;  // x of the stem, in painter coordinates
        qreal beam_y     = 0.0;  // y where a primary (level-1) beam sits —
                                 // same value as the top of this note's own
                                 // stem, i.e. where its flag would start
        qreal notehead_h = 0.0;  // used to size beam thickness/spacing
                                 // consistently with the notehead
    };

    // Paints just the rhythmic symbol (note glyph + augmentation dot)
    // into the given rect. Called by BarRenderer for duration-mode bars.
    // suppress_flag: when true, skip drawing the individual eighth/sixteenth
    // flag — the caller has decided this note is part of a beam group and
    // will draw a shared beam across it and its neighbours instead. Has no
    // effect on notes that wouldn't have a flag anyway (quarter and longer).
    // Returns stem geometry so the caller can draw that beam.
    static StemInfo paint_rhythm(QPainter& painter,
                             const QRectF& rect,
                             const model::chord& ch,
                             const Fonts& fonts,
                             bool suppress_flag = false);

    // Paints only the above-number articulations (staccato, push, tie) into
    // artRect, centred on number_center_x.  Called by bar_renderer for
    // duration-mode bars where the art zone lives above the chord slot.
    static void paint_articulations(QPainter& painter,
                                    const QRectF& artRect,
                                    const model::chord& ch,
                                    const Fonts& fonts,
                                    qreal number_center_x,
                                    qreal row_right);

private:
    // --- Articulation helpers ---
    static void paint_staccato(QPainter& painter, const QRectF& artRect,
                               qreal number_center_x);
    static void paint_pushed(QPainter& painter, const QRectF& artRect,
                             const Fonts& fonts, qreal number_center_x,
                             qreal number_top_y);
    static void paint_tied_arc(QPainter& painter, const QRectF& artRect,
                               qreal diamond_left = -1.0, qreal diamond_right = -1.0);
    static void paint_diamond(QPainter& painter, const QRectF& numberRect, qreal art_bottom, qreal max_bottom, const Fonts& fonts);

    // --- Number row helpers ---

    // Returns the rect actually occupied by the number glyph (used for diamond).
    // Also sets row_right to the x coordinate just past the last rendered element.
    // Renders the full chord symbol left-to-right.  Returns the tight ink
    // rect of the *number* glyph (used to centre articulations); sets
    // row_right to the right edge of the whole row, and chord_extent to the
    // tight ink bounds of the *entire* symbol (number + step + mode +
    // extensions + bass), which is what the diamond is drawn around so the
    // extensions sit inside it.
    static QRectF paint_number_row(QPainter& painter,
                                 const QRectF& rowRect,
                                 const model::chord& ch,
                                 const Fonts& fonts,
                                 qreal& row_right,
                                 QRectF& chord_extent);

    // Renders ♭ or ♯ as a Unicode glyph, returns width consumed.
    static qreal paint_step(QPainter& painter,
                           const model::chord& ch,
                           const Fonts& fonts,
                           qreal x, qreal baseline);

    // Vertical position for a ♭/♯ glyph so its own ink is centred on the ink
    // of the digit it sits beside, rather than sharing that digit's baseline.
    // Common fonts draw accidentals with their ink centred near the
    // x-height — well below a digit's cap-height centre — so a same-baseline
    // draw reads as low, and because the accidental's ink is shorter than the
    // digit's, undersized as well.  ref_fm/glyph_fm let this compare a large
    // number-font digit against a small modifier-font accidental (the leading
    // step) as readily as a same-font pair (extensions, bass note): centring
    // is what matters, not any fixed ratio between the two sizes.  Used
    // everywhere a step glyph is drawn, so all three read identically.
    static qreal accidental_baseline(qreal digit_baseline,
                                     const QFontMetricsF& ref_fm,
                                     const QFontMetricsF& glyph_fm,
                                     const QString& glyph);

    // Renders mode suffix (-, °, +), returns width consumed.
    static qreal paint_mode(QPainter& painter,
                           const model::chord& ch,
                           const Fonts& fonts,
                           qreal x, qreal baseline);

    // Renders extensions string, returns width consumed.
    static qreal paint_extensions(QPainter& painter,
                                 const model::chord& ch,
                                 const Fonts& fonts,
                                 qreal x, qreal baseline);

    // Renders /[♭/#]bassNote, returns width consumed.
    static qreal paint_bass_note(QPainter& painter,
                               const model::chord& ch,
                               const Fonts& fonts,
                               qreal x, qreal baseline);

    // --- Rhythm helpers ---
    static bool is_dotted(model::chord::time duration);

    // Rest rendering.  Rests are drawn entirely in the rhythm row (below the
    // rule), never in the chord-number zone.  paint_rest dispatches off the
    // chord's duration (absent -> whole rest) to the right Bravura glyph.
    static void paint_rest(QPainter& painter,
                           const QRectF& rect,
                           const model::chord& ch,
                           const Fonts& fonts);

    // SMuFL codepoint for the rest of a given duration.  Whole and half use
    // the leger-line variants (restWholeLegerLine / restHalfLegerLine) so the
    // short line — above for whole, below for half — distinguishes them with
    // no staff present.
    static QString rest_glyph_for(model::chord::time duration);

    // The music (Bravura) font scaled so the reference quarter rest occupies
    // k_rest_target_ratio of row_h_px, preserving SMuFL's relative rest sizes.
    static QFont rest_font(const Fonts& fonts, qreal row_h_px);

    // Nominal rhythm-row height in px, used by size_hint to reserve rest width.
    // Must match bar_renderer's k_rhythm_row_px so reserved and painted widths
    // agree.
    static constexpr qreal k_rhythm_row_px    = 16.0;
    // Fraction of the rhythm row the tallest (quarter) rest fills.
    static constexpr qreal k_rest_target_ratio = 0.82;

    // Articulation vertical offsets within the articulation zone (top = 0).
    static constexpr qreal k_staccato_top_ratio = 0.08;
    static constexpr qreal k_tied_top_ratio      = 0.62;
    // Depth of the tie arc's bulge as a fraction of the zone, and how far the
    // bezier control points are pushed above arc_top so the drawn curve
    // actually reaches it.  Named because articulation_headroom needs the same
    // numbers to know where the arc's ink starts.
    static constexpr qreal k_tied_span_ratio     = 0.40;
    static constexpr qreal k_tied_overshoot      = 0.15;
    // Radius of the staccato diamond, and its clearance above the floor of
    // the rect it is drawn in.
    static constexpr qreal k_staccato_dot_r      = 3.0;
    static constexpr qreal k_staccato_floor_gap  = 1.0;

    // Extra horizontal padding between chord elements
    static constexpr qreal k_element_spacing = 2.0;
};

} // namespace nashville::view
