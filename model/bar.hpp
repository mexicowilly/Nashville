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
    enum class repeat_status
    {
        NONE,
        BEGIN,
        END
    };

    bar();

    chord& add_chord();
    bar& add_volta(unsigned v);
    bar& clear_voltas();
    const std::vector<chord>& chords() const;
    bar& chords(const std::vector<chord>& cs);
    bool empty() const;
    bool is_eol() const;
    bar& is_eol(bool state);
    bar& parse_user_input(const std::string& str);
    repeat_status repeat() const;
    bar& repeat(repeat_status st);
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
    repeat_status repeat_ = repeat_status::NONE;
    // These are indexed from 0, even though they are numbered from
    // 1 in the UI
    std::set<unsigned> voltas_;
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

inline bar::repeat_status bar::repeat() const
{
    return repeat_;
}

inline bar& bar::repeat(repeat_status st)
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
