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
    bar& chords(const std::vector<chord>& cs);
    bool empty() const;
    bool is_eol() const;
    bar& is_eol(bool state);
    bar& parse_user_input(const std::string& str);
    const std::optional<std::string>& section() const;
    bar& section(const std::string& sec);
    const std::optional<time_signature>& time_sig() const;
    bar& time_sig(const time_signature& ts);
    std::string to_user_input() const;

private:
    std::vector<chord> chords_;
    std::optional<time_signature> time_signature_;
    bool is_eol_ = false;
    std::optional<std::string> section_;
};

inline const std::vector<chord>& bar::chords() const
{
    return chords_;
}

inline bar& bar::chords(const std::vector<chord>& cs)
{
    chords_ = cs;
    return *this;
}

inline bool bar::empty() const
{
    return chords_.empty();
}

inline bool bar::is_eol() const
{
    return is_eol_;
}

inline const std::optional<std::string>& bar::section() const
{
    return section_;
}

inline const std::optional<time_signature>& bar::time_sig() const
{
    return time_signature_;
}

}
