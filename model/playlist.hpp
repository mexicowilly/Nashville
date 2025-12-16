#pragma once

#include <chucho/loggable.hpp>
#include "song.hpp"

namespace nashville::model
{

class playlist : chucho::loggable<playlist>
{
public:
    playlist(const std::string& name);

    const std::string& name() const;
    playlist& name(const std::string& name);

private:
    std::string name_;
    std::vector<std::reference_wrapper<song>> songs_;
};

inline playlist::playlist(const std::string& name)
    : name_(name)
{
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

}
