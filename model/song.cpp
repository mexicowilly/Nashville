#include "song.hpp"

namespace nashville::model
{

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

song& song::tempo(const std::tuple<unsigned, chord::time>& t)
{
    tempo_ = t;
    return *this;
}

song& song::title(const std::string& t)
{
    title_ = t;
    return *this;
}

song& song::time_sig(const time_signature& t)
{
    time_signature_ = t;
    return *this;
}

}
