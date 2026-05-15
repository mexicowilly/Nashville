#include "bar.hpp"
#include <cstring>
#include <stdexcept>

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
    lgr()->debug("Parsing bar input: '{}'", str);
    std::string src(str);
    std::vector<chord> new_chords;
    auto token = std::strtok(const_cast<char*>(src.c_str()), " ");
    while (token != nullptr)
    {
        new_chords.emplace_back(chord()).parse_user_input(token);
        token = std::strtok(nullptr, " ");
    }
    // A bar must contain at least one chord.  Empty or whitespace-only
    // input would silently produce a chordless bar, which the rest of
    // the system treats as a degenerate edge case (rendered as nothing,
    // emitted as an empty string by to_user_input, etc.).  Reject it at
    // the source so callers everywhere — view, file loader, tests —
    // share one consistent invariant.
    if (new_chords.empty())
        throw std::invalid_argument("A bar must contain at least one chord");
    chords_ = new_chords;
    lgr()->debug("Done");
    return *this;
}

// Setting the section to an empty string is the canonical way to remove
// it.  A bar whose section is the empty string has no meaningful label,
// so we collapse that case to nullopt rather than storing it as a present-
// but-empty optional.  This means callers can write
//   bar.section("Verse")  // assign
//   bar.section("")       // clear
// and the rendering / serialization paths see a single representation of
// "no section" regardless of which path produced it.
bar& bar::section(const std::string& sec)
{
    if (sec.empty())
        section_.reset();
    else
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
