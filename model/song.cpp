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
    metadata_.creation_time = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    metadata_.modification_time = metadata_.creation_time;
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

}
