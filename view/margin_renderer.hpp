#pragma once

#include "../model/song.hpp"
#include <QPainter>
#include <QRectF>

namespace nashville::view
{

// Stateless renderer for the left margin strip.
// Paints key circle, time signature, and tempo grouped near the top.
class margin_renderer
{
public:
    static void paint(QPainter& painter,
                      const QRectF& margin_rect,
                      const model::song& song);

private:
    static void parse_key(const std::string& key,
                          QString& letter,
                          QString& accidental);

    static QString tempo_glyph(model::chord::time beat_unit);

    static constexpr qreal k_element_spacing = 12.0;
    static constexpr qreal k_top_padding    = 16.0;
    static constexpr qreal k_circle_padding = 8.0;
};

} // namespace nashville::view
