#pragma once

#include <chucho/loggable.hpp>
#include "bar.hpp"
#include <tuple>

namespace nashville::model
{

class song : chucho::loggable<song>
{
public:
    bar& add_bar();
    const std::vector<bar>& bars() const;
    unsigned bars_per_line() const;
    song& bars_per_line(unsigned num);
    bool empty() const;
    const std::string& key() const;
    song& key(const std::string& k);
    const std::tuple<unsigned, chord::time>& tempo() const;
    song& tempo(const std::tuple<unsigned, chord::time>& t);
    const time_signature& time_sig() const;
    song& time_sig(const time_signature& t);
    const std::string& title() const;
    song& title(const std::string& t);

private:
    std::vector<bar> bars_;
    std::string title_;
    std::string key_;
    time_signature time_signature_;
    std::tuple<unsigned, chord::time> tempo_;
    unsigned bars_per_line_ = 4;
};

inline bar& song::add_bar()
{
    return bars_.emplace_back(bar());
}

inline unsigned song::bars_per_line() const
{
    return bars_per_line_;
}

inline bool song::empty() const
{
    return bars_.empty();
}

inline const std::string& song::key() const
{
    return key_;
}

inline const std::vector<bar>& song::bars() const
{
    return bars_;
}

inline const std::tuple<unsigned, chord::time>& song::tempo() const
{
    return tempo_;
}

inline const time_signature& song::time_sig() const
{
    return time_signature_;
}

inline const std::string& song::title() const
{
    return title_;
}

}
