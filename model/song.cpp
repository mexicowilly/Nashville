#include "song.hpp"

namespace nashville::model
{

song::song(const std::string& nm)
    : loggable("song"),
      name_(nm),
      tempo_({ 88, chord::time::QUARTER })
{
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
