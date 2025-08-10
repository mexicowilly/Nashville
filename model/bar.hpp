#pragma once

#include "chord.hpp"
#include <chucho/loggable.hpp>
#include <vector>

namespace nashville::model
{

class bar : public chucho::loggable<bar>
{
public:
    chord& add_chord();
    const std::vector<chord>& chords() const;
    bool empty() const;
    void parse_user_input(const std::string& str);
    std::string to_user_input() const;

private:
    std::vector<chord> chords_;
};

inline const std::vector<chord>& bar::chords() const
{
    return chords_;
}

inline bool bar::empty() const
{
    return chords_.empty();
}

}
