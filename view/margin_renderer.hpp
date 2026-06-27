#pragma once

#include "../model/song.hpp"
#include <QPainter>
#include <QRectF>

namespace nashville::view
{

// Bounding rectangles of the three painted margin elements, populated by
// margin_renderer::paint().  Used by song_body_widget to hit-test clicks.
struct margin_layout
{
    QRectF key_rect;             // Around the key circle
    QRectF time_sig_rect;        // Around the stacked numerator/denominator
    QRectF tempo_rect;           // Around the entire note-glyph + " = " + BPM line
    QRectF tempo_glyph_rect;     // Around the note glyph alone (clicked for note-value menu)
    QRectF tempo_bpm_rect;       // Around the BPM number alone (clicked for inline edit)
};

// Stateless renderer for the left margin strip.
// Paints key circle, time signature, and tempo grouped near the top.
//
// Empty fields (currently only key) render in placeholder gray, since the
// chart is always editable — there is no read-only display mode.
class margin_renderer
{
public:
    // out_layout, if non-null, is filled with the bounding rects of the
    // three elements as drawn — caller uses these for hit-testing.
    // Pass nullptr when painting to a transformed coordinate space (e.g.
    // printing) since hit rects must remain in widget coordinates.
    // font_scale multiplies the margin's base font sizes so the key /
    // time signature / tempo grow with the rest of the chart.
    static void paint(QPainter& painter,
                      const QRectF& margin_rect,
                      const model::song& song,
                      margin_layout* out_layout = nullptr,
                      qreal font_scale = 1.0);

private:
    static void parse_key(const std::string& key,
                          QString& letter,
                          QString& accidental,
                          QString& suffix);

    static QString tempo_glyph(model::chord::time beat_unit);

    static constexpr qreal k_element_spacing = 12.0;
    static constexpr qreal k_top_padding    = 16.0;
    static constexpr qreal k_circle_padding = 8.0;
};

} // namespace nashville::view
