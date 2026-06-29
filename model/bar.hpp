#pragma once

#include "chord.hpp"
#include "time_signature.hpp"
#include "loggable.hpp"
#include <vector>
#include <optional>
#include <set>

namespace nashville::model
{

class bar : public loggable
{
public:
    enum repeat_status
    {
        NONE = 0,
        BEGIN = 1,
        END = 2
    };

    bar();

    // Content equality: true iff every musical field matches (chords —
    // compared via chord::operator==, which covers extensions,
    // articulations, ties, pushes and durations — plus the bar-level
    // time signature, line-break flag, section label, repeat flags,
    // voltas, custom beat count and modulation).  Used by
    // song::same_content_as to decide whether the song's modification
    // time should advance on save.  loggable carries no comparable state.
    bool operator==(const bar& other) const;
    bool operator!=(const bar& other) const { return !(*this == other); }

    chord& add_chord();
    bar& add_volta(unsigned v);
    bar& clear_voltas();
    const std::vector<chord>& chords() const;
    bar& chords(const std::vector<chord>& cs);
    bool empty() const;
    bool is_eol() const;
    bar& is_eol(bool state);
    const std::string& modulation() const;
    bar& modulation(const std::string& mod);
    const std::optional<unsigned>& number_of_beats() const;
    bar& number_of_beats(const std::optional<unsigned>& num);
    bar& parse_user_input(const std::string& str);
    // This is checked by ANDing against the enum values. A bar
    // can be both the beginning and the end of a repeat.
    int repeat() const;
    // This is set by ORing the enum values. A bar can be both
    // the beginning and the end of a repeat.
    bar& repeat(int st);
    const std::optional<std::string>& section() const;
    // Assigns a section label.  Passing the empty string clears the
    // section (the optional becomes nullopt) — the empty string is not
    // a valid label.
    bar& section(const std::string& sec);
    const std::optional<time_signature>& time_sig() const;
    bar& time_sig(const time_signature& ts);
    std::string to_user_input() const;
    const std::set<unsigned>& voltas() const;

private:
    std::vector<chord> chords_;
    std::optional<time_signature> time_signature_;
    bool is_eol_ = false;
    std::optional<std::string> section_;
    int repeat_ = repeat_status::NONE;
    // These are indexed from 0, even though they are numbered from
    // 1 in the UI
    std::set<unsigned> voltas_;
    // If left unset, then the number is taken from the count
    // of the time signature of the song.
    std::optional<unsigned> number_of_beats_;
    // The name of the new key. Empty means no modulation.
    std::string modulation_;
};

inline bar& bar::add_volta(unsigned v)
{
    voltas_.insert(v);
    return *this;
}

inline const std::vector<chord>& bar::chords() const
{
    return chords_;
}

inline bar& bar::chords(const std::vector<chord>& cs)
{
    chords_ = cs;
    return *this;
}

inline bar& bar::clear_voltas()
{
    voltas_.clear();
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

inline const std::string& bar::modulation() const
{
    return modulation_;
}

inline bar& bar::modulation(const std::string& mod)
{
    modulation_ = mod;
    return *this;
}

inline const std::optional<unsigned>& bar::number_of_beats() const
{
    return number_of_beats_;
}

inline bar& bar::number_of_beats(const std::optional<unsigned>& num)
{
    number_of_beats_ = num;
    return *this;
}

inline int bar::repeat() const
{
    return repeat_;
}

inline bar& bar::repeat(int st)
{
    repeat_ = st;
    return *this;
}

inline const std::optional<std::string>& bar::section() const
{
    return section_;
}

inline const std::optional<time_signature>& bar::time_sig() const
{
    return time_signature_;
}

inline const std::set<unsigned>& bar::voltas() const
{
    return voltas_;
}

}
