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

bool song::same_content_as(const song& other) const
{
    // Cheap scalar/string/container comparisons first, then the heavier
    // bars and annotations.  creation_time and modification_time are
    // intentionally omitted (see header).
    if (name_           != other.name_)           return false;
    if (key_            != other.key_)            return false;
    if (!(time_signature_ == other.time_signature_)) return false;
    if (tempo_          != other.tempo_)          return false;
    if (bars_per_line_  != other.bars_per_line_)  return false;
    if (margin_width_   != other.margin_width_)   return false;

    const metadata& a = metadata_;
    const metadata& b = other.metadata_;
    if (a.authors                     != b.authors)                     return false;
    if (a.original_performer          != b.original_performer)          return false;
    if (a.original_album              != b.original_album)              return false;
    if (a.notes                       != b.notes)                       return false;
    if (a.original_album_release_date != b.original_album_release_date) return false;

    if (!annotations_.same_content_as(other.annotations_)) return false;

    // bars_ last: vector== short-circuits on size, then defers to
    // bar::operator== element-wise.
    return bars_ == other.bars_;
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
