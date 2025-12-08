#pragma once

#include <chucho/loggable.hpp>
#include "song.hpp"

namespace nashville::model
{

class playlist : chucho::loggable<playlist>
{
private:
    std::string name_;
    std::vector<std::reference_wrapper<song>> songs_;
};

}
