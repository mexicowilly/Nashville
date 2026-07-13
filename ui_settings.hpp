#pragma once

// Persisted UI / appearance settings for the chart view.
//
// Storage is a plain INI file inside the platform's per-user application
// config directory — QStandardPaths::AppConfigLocation, the same directory
// main() creates on startup.  We address that file explicitly (rather than
// letting QSettings derive a path from the organization / application
// names) so the file lands exactly in the configured config dir regardless
// of the platform-specific path conventions QSettings would otherwise use.
//
// Settings stored today:
//   * The chart font scale is a single multiplier applied to every font on
//     the chart: the chord numbers and their satellites (mode / extensions
//     / bass / accidental), the rhythm-row glyphs, the title, the section
//     labels, text boxes, and the left-margin key / time-signature / tempo.
//     One knob keeps the UX simple — the user picks an overall size and
//     the whole chart grows as a unit, rather than tuning a dozen
//     interacting sizes by hand.
//   * The page size and margins are app-wide (not per-song) settings that
//     drive the print-preview chart view: what paper size charts are laid
//     out for, and how much blank border surrounds the content on every
//     side.  Stored internally in points (1/72in) regardless of what unit
//     the Page Setup dialog happens to be showing — see
//     page_geometry.hpp for the geometry these feed into.
//   * The margin display unit is Auto (resolved from the user's locale —
//     inches in the US, centimeters elsewhere) by default, with an explicit
//     override for people who want the other one regardless of locale.

#include "page_geometry.hpp"

namespace nashville::ui_settings
{

// QSettings key for the chart font scale.
inline constexpr const char* FONT_SCALE_KEY = "ui/font_scale";

// Default scale.  1.0 == the sizes the app shipped with.
inline constexpr double FONT_SCALE_DEFAULT = 1.0;

// Clamp range.  We never go below 1.0 — the feature exists to make charts
// more readable, not to cram more onto the page — and cap at 2.0, beyond
// which the margin column and line wrapping stop being useful.
inline constexpr double FONT_SCALE_MIN = 1.0;
inline constexpr double FONT_SCALE_MAX = 2.0;

// Read the persisted font scale, clamped to [MIN, MAX].  Returns the
// default when nothing is stored or the stored value is unparseable.
double font_scale();

// Persist a new font scale (clamped to range) and flush it to disk.
void set_font_scale(double scale);

// --- Page size / margins / units --------------------------------------

// Persisted keys.  Margins are stored as four separate doubles (points)
// rather than one blob so a hand-edited settings.ini stays readable.
inline constexpr const char* PAGE_SIZE_KEY     = "page/size";       // "letter" | "a4"
inline constexpr const char* MARGIN_TOP_KEY    = "page/margin_top_pt";
inline constexpr const char* MARGIN_BOTTOM_KEY = "page/margin_bottom_pt";
inline constexpr const char* MARGIN_LEFT_KEY   = "page/margin_left_pt";
inline constexpr const char* MARGIN_RIGHT_KEY  = "page/margin_right_pt";
inline constexpr const char* LENGTH_UNIT_KEY   = "page/length_unit";  // "auto" | "in" | "cm"

// Sensible defaults: US Letter, 1 inch on every side.
inline constexpr nashville::page_size PAGE_SIZE_DEFAULT = nashville::page_size::letter;
inline constexpr double MARGIN_DEFAULT_PT = 0.5 * k_points_per_inch;  // 1in

// Margins are clamped to a sane range: never negative, and never so large
// that a page could end up with no printable area at all (which would
// send the pagination engine into "every line is its own page" territory
// and make the chart effectively unreadable/unprintable).  3 inches is a
// generous ceiling — comfortably more than anyone would want for a chart
// — while still leaving room for content.
inline constexpr double MARGIN_MIN_PT = 0.0;
inline constexpr double MARGIN_MAX_PT = 3.0 * k_points_per_inch;

// The persisted page size (Letter or A4).  Falls back to the default if
// nothing is stored or the stored value is unrecognized.
nashville::page_size page_size();
void set_page_size(nashville::page_size sz);

// The persisted margins, in points, each clamped to [MARGIN_MIN_PT,
// MARGIN_MAX_PT].  Falls back to MARGIN_DEFAULT_PT per side.
nashville::page_margins margins();
void set_margins(const nashville::page_margins& m);

// "Auto" resolves via the user's locale (QLocale::system(): imperial
// locales — chiefly the US — get inches, everyone else gets centimeters).
// An explicit override always wins over locale.
enum class length_unit_pref { auto_detect, inches, centimeters };

length_unit_pref length_unit_preference();
void set_length_unit_preference(length_unit_pref pref);

// Resolves auto_detect down to a concrete length_unit via the
// system locale, so callers that just want to know "inches or cm right
// now" don't each need to duplicate the locale check.
nashville::length_unit effective_length_unit();

} // namespace nashville::ui_settings
