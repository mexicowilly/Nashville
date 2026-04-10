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
                      bool is_duration_mode = false);

    // Paints just the rhythmic symbol (note glyph + augmentation dot)
    // into the given rect. Called by BarRenderer for duration-mode bars.
    static void paint_rhythm(QPainter& painter,
                             const QRectF& rect,
                             const model::chord& ch,
                             const Fonts& fonts);

private:
    // --- Articulation helpers ---
    static void paint_staccato(QPainter& painter, const QRectF& artRect,
                               qreal number_center_x);
    static void paint_pushed(QPainter& painter, const QRectF& artRect, const Fonts& fonts);
    static void paint_tied_arc(QPainter& painter, const QRectF& artRect,
                               qreal diamond_left = -1.0, qreal diamond_right = -1.0);
    static void paint_diamond(QPainter& painter, const QRectF& numberRect);

    // --- Number row helpers ---

    // Returns the rect actually occupied by the number glyph (used for diamond).
    static QRectF paint_number_row(QPainter& painter,
                                 const QRectF& rowRect,
                                 const model::chord& ch,
                                 const Fonts& fonts);

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
    static QString note_glyph(model::chord::time duration);
    static bool is_dotted(model::chord::time duration);

    // Articulation vertical offsets within the articulation zone (top = 0).
    static constexpr qreal k_staccato_top_ratio = 0.08;
    static constexpr qreal k_pushed_top_ratio    = 0.35;
    static constexpr qreal k_tied_top_ratio      = 0.62;

    // Diamond padding around the number glyph rect.
    // Horizontal padding is larger than vertical so the diamond looks
    // proportionally wide rather than pinched.
    static constexpr qreal k_diamond_padding_h = 10.0;  // left/right
    static constexpr qreal k_diamond_padding_v =  5.0;  // top/bottom

    // Extra horizontal padding between chord elements
    static constexpr qreal k_element_spacing = 2.0;
};

} // namespace nashville::view
