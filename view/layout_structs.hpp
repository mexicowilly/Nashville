#pragma once

#include "../model/bar.hpp"
#include <QRectF>
#include <QString>
#include <vector>
#include <optional>

namespace nashville::view
{

struct BarLayout
{
    const model::bar* bar    = nullptr;
    QRectF rect;
    bool isDurationMode      = false;  // cached from bar contents
    bool showContinuationDot = false;  // paint dot in gap after this bar
};

struct LineLayout
{
    std::vector<BarLayout> bars;
    QRectF rect;                           // full line rect including all bars
    std::optional<QString> sectionLabel;   // from first bar that has section_
    bool isDurationMode = false;           // true if ANY bar on this line is duration-mode
};

} // namespace nashville::view
