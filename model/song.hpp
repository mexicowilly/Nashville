#pragma once

#include "loggable.hpp"
#include "bar.hpp"
#include <tuple>

namespace nashville::model
{

class song : public loggable
{
public:
    song();
    song(const std::string& nm);

    bar& add_bar();
    const std::vector<bar>& bars() const;
    song& bars(const std::vector<bar>& bs);
    unsigned bars_per_line() const;
    song& bars_per_line(unsigned num);
    bool empty() const;
    const std::string& key() const;
    song& key(const std::string& k);
    const std::string& name() const;
    song& name(const std::string& t);
    const std::tuple<unsigned, chord::time>& tempo() const;
    song& tempo(const std::tuple<unsigned, chord::time>& t);
    const time_signature& time_sig() const;
    song& time_sig(const time_signature& t);
    // Each key of the map corresponds to the index of
    // a volta in the song. The vector contains the bar
    // indexes that appear in that volta.
    std::map<unsigned, std::vector<unsigned>> voltas() const;

private:
    std::vector<bar> bars_;
    std::string name_;
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

inline song& song::bars(const std::vector<bar>& bs)
{
    bars_ = bs;
    return *this;
}

inline const std::string& song::name() const
{
    return name_;
}

inline const std::tuple<unsigned, chord::time>& song::tempo() const
{
    return tempo_;
}

inline const time_signature& song::time_sig() const
{
    return time_signature_;
}

}
