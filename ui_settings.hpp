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
// Only the chart font scale is stored today.  The scale is a single
// multiplier applied to every font on the chart: the chord numbers and
// their satellites (mode / extensions / bass / accidental), the rhythm-row
// glyphs, the title, the section labels, text boxes, and the left-margin
// key / time-signature / tempo.  One knob keeps the UX simple — the user
// picks an overall size and the whole chart grows as a unit, rather than
// tuning a dozen interacting sizes by hand.

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

} // namespace nashville::ui_settings
