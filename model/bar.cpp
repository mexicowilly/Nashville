#include "bar.hpp"
#include <chucho/log.hpp>
#include <cstring>

namespace nashville::model
{

chord& bar::add_chord()
{
    return chords_.emplace_back(chord());
}

bar& bar::is_eol(bool state)
{
    is_eol_ = state;
    return *this;
}

void bar::parse_user_input(const std::string& str)
{
    CHUCHO_DEBUG_L("Parsing bar input: '" << str << "'");
    std::string src(str);
    std::vector<chord> new_chords;
    auto token = std::strtok(const_cast<char*>(src.c_str()), " ");
    while (token != nullptr)
    {
        new_chords.emplace_back(chord()).parse_user_input(token);
        token = std::strtok(nullptr, " ");
    }
    chords_ = new_chords;
    CHUCHO_DEBUG_L("Done");
}

bar& bar::time_sig(const time_signature& ts)
{
    time_signature_ = ts;
    return *this;
}

std::string bar::to_user_input() const
{
    std::ostringstream out;
    for (const auto& ch : chords_)
        out << ch.to_user_input() << ' ';
    auto inp = out.str();
    if (!inp.empty())
        inp.pop_back();
    return inp;
}

}
