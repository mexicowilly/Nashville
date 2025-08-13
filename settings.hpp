#pragma once

#include <QSettings>

namespace nashville::settings
{

enum
{
    MINOR_DASH,
    MINOR_LETTER_M,
    MINOR_ABBREV_MIN
};

const char* MINOR_CHORD_STYLE = "minor chord style";
const char* SHOW_CHORD_SYMBOLS = "show chord symbols";
const char* BAR_UNDERLINE_THICKNESS = "bar underline thickness";

namespace defaults
{

bool SHOW_CHORD_SYMBOLS = true;
int MINOR_CHORD_STYLE = MINOR_DASH;
unsigned BAR_UNDERLINE_THICKNESS = 3;

}

}
