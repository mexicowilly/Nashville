#pragma once

#include "chord.hpp"
#include "time_signature.hpp"
#include <chucho/loggable.hpp>
#include <vector>
#include <optional>

namespace nashville::model
{

class bar : chucho::loggable<bar>
{
public:
    chord& add_chord();
    const std::vector<chord>& chords() const;
    bool empty() const;
    void parse_user_input(const std::string& str);
    const std::optional<time_signature>& time_sig() const;
    bar& time_sig(const time_signature& ts);
    std::string to_user_input() const;

private:
    std::vector<chord> chords_;
    std::optional<time_signature> time_signature_;
};

inline const std::vector<chord>& bar::chords() const
{
    return chords_;
}

inline bool bar::empty() const
{
    return chords_.empty();
}

inline const std::optional<time_signature>& bar::time_sig() const
{
    return time_signature_;
}

inline bar& bar::time_sig(const time_signature& ts)
{
    time_signature_ = ts;
    return *this;
}

}
