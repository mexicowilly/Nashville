#include "song.hpp"

namespace nashville::model
{

song::song()
    : song("Title")
{
    name_.clear();
}

song::song(const std::string& nm)
    : loggable("song"),
      name_(nm),
      tempo_({ 88, chord::time::QUARTER })
{
    if (nm.empty())
        throw std::invalid_argument("The song name cannot be empty");
}

song& song::bars_per_line(unsigned num)
{
    bars_per_line_= num;
    return *this;
}

song& song::key(const std::string& k)
{
    key_ = k;
    return *this;
}

song& song::name(const std::string& n)
{
    if (n.empty())
        throw std::invalid_argument("The song name cannot be empty");
    name_ = n;
    return *this;
}

song& song::tempo(const std::tuple<unsigned, chord::time>& t)
{
    tempo_ = t;
    return *this;
}

song& song::time_sig(const time_signature& t)
{
    time_signature_ = t;
    return *this;
}

std::map<unsigned, std::vector<unsigned>> song::voltas() const
{
    std::map<unsigned, std::vector<unsigned>> all;
    for (unsigned i = 0; i < bars_.size(); i++)
    {
        for (auto bar_volta : bars_[i].voltas())
            all[bar_volta].push_back(i);
    }
    lgr()->debug("Found {} voltas in song '{}'", all.size(), name_);
    return all;
}

}
