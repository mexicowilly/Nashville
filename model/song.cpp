#include "song.hpp"

namespace nashville::model
{

song& song::key(const std::string& k)
{
    key_ = k;
    return *this;
}

song& song::tempo(unsigned t)
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
