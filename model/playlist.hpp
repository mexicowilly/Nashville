#pragma once

#include <chucho/loggable.hpp>
#include "song.hpp"

namespace nashville::model
{

class playlist : chucho::loggable<playlist>
{
public:
    playlist(const std::string& name);

    playlist& add_song(const song& s);
    const std::string& name() const;
    playlist& name(const std::string& name);
    const std::vector<std::reference_wrapper<const song>> songs() const;

private:
    std::string name_;
    std::vector<std::reference_wrapper<const song>> songs_;
};

inline playlist::playlist(const std::string& name)
    : name_(name)
{
}

inline playlist& playlist::add_song(const song& s)
{
    songs_.emplace_back(std::cref(s));
    return *this;
}

inline const std::string& playlist::name() const
{
    return name_;
}

inline playlist& playlist::name(const std::string& name)
{
    name_ = name;
    return *this;
}

inline const std::vector<std::reference_wrapper<const song>> playlist::songs() const
{
    return songs_;
}

}
