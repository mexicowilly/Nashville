#include "chord.hpp"
#include <stdexcept>
#include <regex>
#include <cassert>
#include <sstream>

namespace nashville::model
{

chord::chord()
    : loggable("chord"),
      number_(1),
      mode_(type::UNDEFINED),
      is_staccato_(false),
      is_diamond_(false),
      is_tied_(false),
      is_pushed_(false)
{
}

bool chord::operator== (const chord& other) const
{
    return number_ == other.number_ &&
           mode_ == other. mode_ &&
           step_ == other.step_ &&
           bass_note_ == other.bass_note_ &&
           bass_note_step_ == other.bass_note_step_ &&
           extensions_ == other.extensions_ &&
           is_staccato_ == other.is_staccato_ &&
           is_diamond_ == other.is_diamond_ &&
           is_tied_ == other.is_tied_ &&
           is_pushed_ == other.is_pushed_ &&
           duration_ == other.duration_;
}

chord& chord::bass_note(unsigned bn)
{
    if (bn < 1 || bn > 7)
        throw std::invalid_argument("The bass note number must be from 1 to 7");
    bass_note_ = bn;
    return *this;
}

chord& chord::bass_note_step(chord::flat_sharp fs)
{
    bass_note_step_ = fs;
    return *this;
}

chord& chord::duration(chord::time dur)
{
    duration_ = dur;
    return *this;
}

chord& chord::extensions(const std::string& ext)
{
    extensions_ = ext;
    return *this;
}

chord& chord::is_diamond(bool state)
{
    is_diamond_ = state;
    if (is_diamond_ && is_staccato_)
    {
        is_staccato_ = false;
        lgr()->info("Removing staccato flag due to diamond being set");
    }
    return *this;
}

chord& chord::is_pushed(bool state)
{
    is_pushed_ = state;
    return *this;
}

chord& chord::is_staccato(bool state)
{
    is_staccato_ = state;
    if (is_staccato_)
    {
        if (is_diamond_)
        {
            is_diamond_ = false;
            lgr()->info("Removing diamond flag due to staccato being set");
        }
        if (is_tied_)
        {
            is_tied_ = false;
            lgr()->info("Removing tied flag due to staccato being set");
        }
    }
    return *this;
}

chord& chord::is_tied(bool state)
{
    is_tied_ = state;
    if (is_tied_ && is_staccato_)
    {
        is_staccato_ = false;
        lgr()->info("Removing staccato flag due to tied being set");
    }
    return *this;
}

chord& chord::mode(chord::type m)
{
    mode_ = m;
    return *this;
}

chord& chord::number(unsigned num)
{
    if (num < 1 || num > 7)
        throw std::invalid_argument("The chord number must be from 1 to 7");
    number_ = num;
    if (mode_ == type::UNDEFINED)
        mode_ = type::MAJOR;
    return *this;
}

chord& chord::parse_user_input(const std::string& usr)
{
    lgr()->debug("Parsing chord input: '{}'", usr);
    if (usr.empty())
        throw std::invalid_argument("The chord description cannot be empty");
    chord saved(*this);
    *this = chord();
    auto re = std::regex("([tdsp]+:)?([b#])?([1-7])(-|dim|\\+)?([^\\/:]+)?(\\/([b#])?([1-7]))?(:([sSeEqQhHwW]\\.?))?");
    std::smatch result;
    if (std::regex_match(usr, result, re))
    {
        if (result[1].length() > 0)
        {
            for (auto c : result[1].str())
            {
                switch (c)
                {
                case 't':
                    is_tied_ = true;
                    break;
                case 'd':
                    is_diamond_ = true;
                    break;
                case 's':
                    is_staccato_ = true;
                    break;
                case 'p':
                    is_pushed_ = true;
                    break;
                default:;
                }
            }
            if (is_staccato_)
            {
                is_tied_ = false;
                is_diamond_ = false;
            }
        }
        if (result[2].length() > 0)
            step_ = result[2].str()[0] == 'b' ? flat_sharp::FLAT : flat_sharp::SHARP;
        assert(result[3].length() == 1);
        number_ = result[3].str()[0] - '0';
        if (result[4].length() > 0)
        {
            auto mode = result[4].str();
            if (mode == "-")
            {
                mode_ = type::MINOR;
            }
            else if (mode == "dim")
            {
                mode_ = type::DIMINISHED;
            }
            else
            {
                assert(mode == "+");
                mode_ = type::AUGMENTED;
            }
        }
        else
        {
            mode_ = type::MAJOR;
        }
        if (result[5].length() > 0)
        {
            extensions_ = result[5].str();
        }
        if (result[6].length() > 0)
        {
            if (result[7].length() > 0)
                bass_note_step_ = result[7].str()[0] == 'b' ? flat_sharp::FLAT : flat_sharp::SHARP;
            assert(result[8].length() == 1);
            bass_note_ = result[8].str()[0] - '0';
        }
        if (result[9].length() > 0)
        {
            assert(result[10].length() == 1 || result[10].length() == 2);
            bool dot = result[10].length() == 2;
            assert((dot && result[10].str()[1] == '.') || !dot);
            switch (result[10].str()[0])
            {
                case 's':
                case 'S':
                    duration_ = time::SIXTEENTH;
                    break;
                case 'e':
                case 'E':
                    duration_ = dot ? time::DOTTED_EIGHTH : time::EIGHTH;
                    break;
                case 'q':
                case 'Q':
                    duration_ = dot ? time::DOTTED_QUARTER : time::QUARTER;
                    break;
                case 'h':
                case 'H':
                    duration_ = dot ? time::DOTTED_HALF : time::HALF;
                    break;
                case 'w':
                case 'W':
                    duration_ = time::WHOLE;
                    break;
            }
        }
    }
    else
    {
        *this = saved;
        throw std::invalid_argument("'" + usr + "' is not a valid chord description");
    }
    lgr()->debug("Found {}", to_string());
    return *this;
}

chord& chord::step(flat_sharp fs)
{
    step_ = fs;
    return *this;
}

std::string chord::to_string() const
{
    std::stringstream out;
    out << "chord:{";
    if (mode_ != chord::type::UNDEFINED)
    {
        if (step_)
            out << (step_ == chord::flat_sharp::FLAT ? "flat " : "sharp ");
        out << number_;
        switch (mode_)
        {
            case chord::type::MINOR:
                out << "min";
                break;
            case chord::type::DIMINISHED:
                out << "dim";
                break;
            case chord::type::AUGMENTED:
                out << "aug";
                break;
            default: ;
        }
        if (!extensions_.empty())
            out << " " << extensions_;
        if (bass_note_)
        {
            out << " / ";
            if (bass_note_step_)
                out << (*bass_note_step_ == chord::flat_sharp::FLAT ? "flat " : "sharp ");
            out << *bass_note_;
        }
        if (is_staccato_ || is_diamond_ || is_tied_ || is_pushed_)
        {
            out << " (";
            if (is_diamond_)
            {
                out << "d";
                if (is_staccato_ || is_tied_ || is_pushed_)
                    out << ",";
            }
            if (is_pushed_)
            {
                out << "p";
                if (is_staccato_ || is_tied_)
                    out << ",";
            }
            if (is_staccato_)
            {
                out << "s";
                if (is_tied_)
                    out << ",";
            }
            if (is_tied_)
                out << "t";
            out << ")";
        }
        if (duration_)
        {
            out << " ";
            switch (*duration_)
            {
                case chord::time::SIXTEENTH:
                    out << "s";
                    break;
                case chord::time::EIGHTH:
                    out << "e";
                    break;
                case chord::time::DOTTED_EIGHTH:
                    out << "e.";
                    break;
                case chord::time::QUARTER:
                    out << "q";
                    break;
                case chord::time::DOTTED_QUARTER:
                    out << "q.";
                    break;
                case chord::time::HALF:
                    out << "h";
                    break;
                case chord::time::DOTTED_HALF:
                    out << "h.";
                    break;
                case chord::time::WHOLE:
                    out << "w";
                    break;
            }
        }
    }
    out << "}";
    return out.str();
}

std::string chord::to_user_input() const
{
    if (mode_ == type::UNDEFINED)
        return std::string();
    std::ostringstream out;
    if (is_diamond_ || is_pushed_ || is_staccato_ || is_tied_)
    {
        if (is_diamond_)
            out << 'd';
        if (is_pushed_)
            out << 'p';
        if (is_staccato_)
            out << 's';
        if (is_tied_)
            out << 't';
        out << ':';
    }
    if (step_)
        out << (*step_ == flat_sharp::FLAT ? 'b' : '#');
    out << number_;
    switch (mode_)
    {
    case type::MINOR:
        out << '-';
        break;
    case type::DIMINISHED:
        out << "dim";
        break;
    case type::AUGMENTED:
        out << '+';
        break;
    default:;
    }
    out << extensions_;
    if (bass_note_)
    {
        out << '/';
        if (bass_note_step_)
            out << (*bass_note_step_ == flat_sharp::FLAT ? 'b' : '#');
        out << *bass_note_;
    }
    if (duration_)
    {
        out << ':';
        switch (*duration_)
        {
        case time::SIXTEENTH:
            out << 's';
            break;
        case time::EIGHTH:
            out << 'e';
            break;
        case time::DOTTED_EIGHTH:
            out << "e.";
            break;
        case time::QUARTER:
            out << 'q';
            break;
        case time::DOTTED_QUARTER:
            out << "q.";
            break;
        case time::HALF:
            out << 'h';
            break;
        case time::DOTTED_HALF:
            out << "h.";
            break;
        case time::WHOLE:
            out << 'w';
        }
    }
    return out.str();
}

}
