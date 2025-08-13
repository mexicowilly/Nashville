#pragma once

namespace nashville::model
{

struct time_signature
{
    enum class beat_type
    {
        EIGHTH,
        DOTTED_EIGHTH,
        QUARTER,
        DOTTED_QUARTER,
        HALF,
        DOTTED_HALF
    };

    beat_type kind;
    unsigned count;
};

}
