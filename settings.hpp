#pragma once

namespace nashville::settings
{

enum class minor_chord_style
{
    DASH,
    LETTER_M,
    ABBREV_MIN
};

namespace defaults
{

bool SHOW_CHORD_SYMBOLS = true;
minor_chord_style MINOR_CHORD_STYLE = minor_chord_style::DASH;

}

}
