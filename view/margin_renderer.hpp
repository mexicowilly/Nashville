#pragma once

#include "../model/song.hpp"
#include <QPainter>
#include <QRectF>

namespace nashville::view
{

// Stateless renderer for the left margin strip.
// Paints key circle, time signature, and tempo grouped near the top.
class MarginRenderer
{
public:
    static void paint(QPainter& painter,
                      const QRectF& marginRect,
                      const model::song& song);

private:
    static void parseKey(const std::string& key,
                         QString& letter,
                         QString& accidental);

    static QString tempoGlyph(model::chord::time beatUnit);

    static constexpr qreal kElementSpacing = 12.0;
    static constexpr qreal kTopPadding     = 16.0;
    static constexpr qreal kCirclePadding  = 8.0;
};

} // namespace nashville::view
