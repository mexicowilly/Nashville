#include "time_signature.hpp"
#include <regex>
#include <stdexcept>
#include <cassert>

namespace nashville::model
{

bool time_signature::operator== (const time_signature& other) const
{
    return kind_ == other.kind_ && count_ == other.count_;
}

void time_signature::parse_user_input(const std::string& inp)
{
    if (inp.empty())
        throw std::invalid_argument("The time signature cannot be empty");
    std::regex re(" *(\\d+) */ *([248]) *");
    std::smatch result;
    if (std::regex_match(inp, result, re))
    {
        count_ = std::stoi(result[1]);
        if (result[2] == "2")
            kind_ = beat_type::HALF;
        else if (result[2] == "4")
            kind_ = beat_type::QUARTER;
        else
            kind_ = beat_type::EIGHTH;
    }
    else
    {
        throw std::invalid_argument("The time signature must consist of \"number/2, 4 or 8\"");
    }
}

}
