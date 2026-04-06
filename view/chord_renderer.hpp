#pragma once

#include "../model/chord.hpp"
#include <QPainter>
#include <QRectF>
#include <QFont>

namespace nashville::view
{

// Stateless renderer for a single chord.
// All methods are static — instantiate only to configure fonts.
class ChordRenderer
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
    static constexpr qreal kNumberZoneRatio      = 0.65;
    static constexpr qreal kArticulationZoneRatio = 0.35;

    // Returns the minimum bounding size needed to render this chord,
    // given the supplied fonts. Used by the layout pass.
    static QSizeF sizeHint(const model::chord& ch, const Fonts& fonts);

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
    static void paintRhythm(QPainter& painter,
                            const QRectF& rect,
                            const model::chord& ch,
                            const Fonts& fonts);

private:
    // --- Articulation helpers ---
    static void paintStaccato(QPainter& painter, const QRectF& artRect);
    static void paintPushed(QPainter& painter, const QRectF& artRect, const Fonts& fonts);
    static void paintTiedArc(QPainter& painter, const QRectF& artRect);
    static void paintDiamond(QPainter& painter, const QRectF& numberRect);

    // --- Number row helpers ---

    // Returns the rect actually occupied by the number glyph (used for diamond).
    static QRectF paintNumberRow(QPainter& painter,
                                 const QRectF& rowRect,
                                 const model::chord& ch,
                                 const Fonts& fonts);

    // Renders ♭ or ♯ as a Unicode glyph, returns width consumed.
    static qreal paintStep(QPainter& painter,
                           const model::chord& ch,
                           const Fonts& fonts,
                           qreal x, qreal baseline);

    // Renders mode suffix (-, °, +), returns width consumed.
    static qreal paintMode(QPainter& painter,
                           const model::chord& ch,
                           const Fonts& fonts,
                           qreal x, qreal baseline);

    // Renders extensions string, returns width consumed.
    static qreal paintExtensions(QPainter& painter,
                                 const model::chord& ch,
                                 const Fonts& fonts,
                                 qreal x, qreal baseline);

    // Renders /[♭/#]bassNote, returns width consumed.
    static qreal paintBassNote(QPainter& painter,
                               const model::chord& ch,
                               const Fonts& fonts,
                               qreal x, qreal baseline);

    // --- Rhythm helpers ---
    static QString noteGlyph(model::chord::time duration);
    static bool isDotted(model::chord::time duration);

    // Articulation vertical offsets within the articulation zone (top = 0).
    static constexpr qreal kStaccatoTopRatio = 0.05;
    static constexpr qreal kPushedTopRatio   = 0.35;
    static constexpr qreal kTiedTopRatio     = 0.62;

    // Diamond padding around number rect
    static constexpr qreal kDiamondPadding = 4.0;

    // Extra horizontal padding between chord elements
    static constexpr qreal kElementSpacing = 2.0;
};

} // namespace nashville::view
