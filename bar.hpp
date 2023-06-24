#pragma once

#include "chord.hpp"
#include <vector>

namespace nashville::model
{

class bar
{
public:
    void add_chord(chord&& ch);
    const std::vector<chord>& chords() const;

private:
    std::vector<chord> chords_;
};

inline void bar::add_chord(chord&& ch)
{
    chords_.emplace_back(ch);
}

inline const std::vector<chord>& bar::chords() const
{
    return chords_;
}

}