#include "bar.hpp"
#include <cstring>

namespace nashville::model
{

bar::bar()
    : loggable("bar")
{
}

chord& bar::add_chord()
{
    return chords_.emplace_back(chord());
}

bar& bar::is_eol(bool state)
{
    is_eol_ = state;
    return *this;
}

bar& bar::parse_user_input(const std::string& str)
{
    lgr()->debug("Parsing bar input: '{}'");
    std::string src(str);
    std::vector<chord> new_chords;
    auto token = std::strtok(const_cast<char*>(src.c_str()), " ");
    while (token != nullptr)
    {
        new_chords.emplace_back(chord()).parse_user_input(token);
        token = std::strtok(nullptr, " ");
    }
    chords_ = new_chords;
    lgr()->debug("Done");
    return *this;
}

bar& bar::section(const std::string& sec)
{
    section_ = sec;
    return *this;
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
