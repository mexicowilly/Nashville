#pragma once

// Page-layout math for the "print preview" chart view: page sizes, margins,
// unit conversions, and the pagination algorithm that decides which page
// each line of a chart lands on.
//
// Deliberately free of QWidget/QPainter — everything here is plain value
// types and pure functions, so it's usable both by the on-screen chart
// widget and by the print path, and is unit-testable without a display.
//
// Internal unit: points (1/72 inch), matching QFont's point-size convention
// and QPrinter's own units — this is why the on-screen chart and the printed
// page can share the exact same numbers instead of two parallel scales.  We
// treat 1 point as 1 logical (device-independent) pixel on screen, which is
// the convention the rest of this codebase already uses for font sizes
// (title_height(), k_title_base_pt, etc. are all "points" used directly as
// widget-space lengths).  Inches/centimeters are display-only conveniences
// converted at the UI boundary (ui_settings, the Page Setup dialog).

#include <QSizeF>
#include <vector>
#include <algorithm>

// This is a root-level leaf utility (alongside iso8601.hpp, ui_settings.hpp):
// pure geometry/pagination math with a single Qt dependency (QSizeF/qreal)
// and no dependency on the view widgets, so it lives in namespace nashville
// rather than nashville::view — both the view layer and app-level config
// (ui_settings) consume it.
namespace nashville
{

enum class page_size
{
    letter,   // 8.5in x 11in
    a4,       // 210mm x 297mm
};

enum class length_unit
{
    inches,
    centimeters,
};

inline constexpr qreal k_points_per_inch = 72.0;
inline constexpr qreal k_mm_per_inch     = 25.4;
inline constexpr qreal k_points_per_cm   = k_points_per_inch * 10.0 / k_mm_per_inch;

// Portrait dimensions in points.  Landscape isn't offered — every chart in
// this app is a vertical stack of lines, which is a portrait-shaped
// document; adding landscape would be a real feature (rotating the whole
// pagination model), not a small extension, so it's left out of this pass.
inline QSizeF page_size_points(page_size sz)
{
    switch (sz)
    {
    case page_size::letter:
        return QSizeF(8.5 * k_points_per_inch, 11.0 * k_points_per_inch);
    case page_size::a4:
        return QSizeF(210.0 / k_mm_per_inch * k_points_per_inch,
                      297.0 / k_mm_per_inch * k_points_per_inch);
    }
    return QSizeF(8.5 * k_points_per_inch, 11.0 * k_points_per_inch);
}

inline qreal to_points(qreal value, length_unit u)
{
    return u == length_unit::inches ? value * k_points_per_inch
                                     : value * k_points_per_cm;
}

inline qreal from_points(qreal points, length_unit u)
{
    return u == length_unit::inches ? points / k_points_per_inch
                                     : points / k_points_per_cm;
}

// Page margins, always stored in points regardless of what unit the UI
// happens to be showing the user.
struct page_margins
{
    qreal top    = k_points_per_inch;
    qreal bottom = k_points_per_inch;
    qreal left   = k_points_per_inch;
    qreal right  = k_points_per_inch;
};

// Visual gap drawn between consecutive page sheets on screen.  Purely a
// display affordance (so pages read as separate sheets rather than one
// continuous strip) — never appears in print output, where a page is
// simply followed by a hard page break.
inline constexpr qreal k_inter_page_gap = 24.0;

struct page_geometry
{
    QSizeF       size    = page_size_points(page_size::letter);
    page_margins margins;

    qreal content_width() const
    {
        return std::max(0.0, size.width() - margins.left - margins.right);
    }
    qreal content_height() const
    {
        return std::max(0.0, size.height() - margins.top - margins.bottom);
    }
};

// Where one flow item (a title block, a chart line, ...) lands once the
// content is broken across pages.
struct flow_position
{
    int   page_index      = 0;    // 0-based
    qreal y_in_page_content = 0.0; // offset from that page's content top
                                    // (i.e. already excludes margins.top)
};

struct pagination_result
{
    std::vector<flow_position> positions;  // one per input item, same order
    int page_count = 1;
};

// Lay a sequence of item heights out across pages of the given content
// height, never splitting an item across a page boundary.  An item taller
// than a full page's content height is placed alone on its own page rather
// than looping forever or being silently dropped.
//
// later_page_top reserves that much vertical space at the top of every page
// *after* the first — the room a repeated running header (e.g. the song
// title plus " pg. N") occupies on continuation pages.  Page 0's own header
// is modelled by the caller as a leading flow item instead (see
// song_body_widget::paginate_lines), so this only shifts pages >= 1.  Zero
// (the default) restores the old single-header behaviour.
//
// content_height <= 0 degenerately places everything on one page (rather
// than infinite-looping trying to start a "next" page that still has no
// room) — callers should treat that as a configuration error to surface
// elsewhere (e.g. margins that exceed the page size), not something this
// function needs to reject.
inline pagination_result paginate(qreal content_height,
                                  const std::vector<qreal>& item_heights,
                                  qreal later_page_top = 0.0)
{
    pagination_result result;
    result.positions.reserve(item_heights.size());

    if (content_height <= 0.0)
    {
        for (qreal h : item_heights)
        {
            (void)h;
            result.positions.push_back({0, 0.0});
        }
        result.page_count = 1;
        return result;
    }

    // A header taller than the page would leave no room for content; ignore
    // it in that degenerate case rather than pushing every line off-page.
    if (later_page_top < 0.0 || later_page_top >= content_height)
        later_page_top = 0.0;

    int page = 0;
    qreal y = 0.0;  // content consumed on the current page so far
    for (qreal h : item_heights)
    {
        // Start a new page if this item doesn't fit AND the current page
        // already has something on it.  The "already has something" guard
        // is what keeps an oversized single item from looping forever —
        // it still gets placed (alone, overflowing), and only the *next*
        // item is pushed to a fresh page.  A fresh page (page >= 1) begins
        // its content below the reserved running-header band.
        if (y > 0.0 && y + h > content_height)
        {
            ++page;
            y = later_page_top;
        }
        result.positions.push_back({page, y});
        y += h;
    }
    result.page_count = page + 1;
    return result;
}

// Absolute canvas y-coordinate for a flow position, given the page
// geometry.  page0_content_top overrides where page 0's content begins
// (the chart title sits above the content on page 0 only — every later
// page starts its content right at the top margin, with no title repeat).
inline qreal absolute_y(const page_geometry& geo, qreal page0_content_top,
                        const flow_position& pos)
{
    const qreal page_top = (pos.page_index == 0)
        ? page0_content_top
        : pos.page_index * (geo.size.height() + k_inter_page_gap) + geo.margins.top;
    return page_top + pos.y_in_page_content;
}

// Total canvas height needed to display page_count pages back-to-back with
// the inter-page gap between them.  Used to size the scrollable canvas.
inline qreal total_canvas_height(const page_geometry& geo, int page_count)
{
    if (page_count < 1) page_count = 1;
    return page_count * geo.size.height() + (page_count - 1) * k_inter_page_gap;
}

} // namespace nashville
