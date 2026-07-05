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

    // Diamond padding around the tight number glyph rect.
    // Public so bar_renderer can account for vertical clearance in slot sizing.
    static constexpr qreal k_diamond_padding_h = 10.0;  // left/right
    static constexpr qreal k_diamond_padding_v =  6.0;  // top/bottom

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
    static void paint_diamond(QPainter& painter, const QRectF& numberRect, qreal art_bottom, qreal max_bottom);

    // --- Number row helpers ---

    // Returns the rect actually occupied by the number glyph (used for diamond).
    // Also sets row_right to the x coordinate just past the last rendered element.
    static QRectF paint_number_row(QPainter& painter,
                                 const QRectF& rowRect,
                                 const model::chord& ch,
                                 const Fonts& fonts,
                                 qreal& row_right);

    // Renders ♭ or ♯ as a Unicode glyph, returns width consumed.
    static qreal paint_step(QPainter& painter,
                           const model::chord& ch,
                           const Fonts& fonts,
                           qreal x, qreal baseline);

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

    // Extra horizontal padding between chord elements
    static constexpr qreal k_element_spacing = 2.0;
};

} // namespace nashville::view
