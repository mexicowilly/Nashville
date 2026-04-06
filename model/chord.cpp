#include "chord.hpp"
#include <chucho/log.hpp>
#include <stdexcept>
#include <regex>
#include <cassert>
#include <sstream>

namespace nashville::model
{

chord::chord()
    : number_(1),
      mode_(type::UNDEFINED),
      is_staccato_(false),
      is_diamond_(false),
      is_tied_(false),
      is_pushed_(false)
{
}

std::ostream& operator<< (std::ostream& out, const chord& c)
{
    out << "chord:{";
    if (c.mode_ != chord::type::UNDEFINED)
    {
        if (c.step_)
            out << (c.step_ == chord::flat_sharp::FLAT ? "flat " : "sharp ");
        out << c.number_;
        switch (c.mode_)
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
        if (!c.extensions_.empty())
            out << " " << c.extensions_;
        if (c.bass_note_)
        {
            out << " / ";
            if (c.bass_note_step_)
                out << (*c.bass_note_step_ == chord::flat_sharp::FLAT ? "flat " : "sharp ");
            out << *c.bass_note_;
        }
        if (c.is_staccato_ || c.is_diamond_ || c.is_tied_)
        {
            out << " (";
            if (c.is_diamond_)
            {
                out << "d";
                if (c.is_staccato_ || c.is_tied_)
                    out << ",";
            }
            if (c.is_staccato_)
            {
                out << "s";
                if (c.is_tied_)
                    out << ",";
            }
            if (c.is_tied_)
                out << "t";
            out << ")";
        }
        if (c.duration_)
        {
            out << " ";
            switch (*c.duration_)
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
    return out;
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
    return *this;
}

chord& chord::is_tied(bool state)
{
    is_tied_ = state;
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
    CHUCHO_DEBUG_L("Parsing chord input: '" << usr << "'");
    if (usr.empty())
        throw std::invalid_argument("The chord description cannot be empty");
    chord saved(*this);
    *this = chord();
    auto re = std::regex("([b#])?([1-7])(-|dim|\\+)?([^\\/:]+)?(\\/([b#])?([1-7]))?(:([sSeEqQhHwW]\\.?))?");
    std::smatch result;
    if (std::regex_match(usr, result, re))
    {
        if (result[1].length() > 0)
            step_ = result[1].str()[0] == 'b' ? flat_sharp::FLAT : flat_sharp::SHARP;
        assert(result[2].length() == 1);
        number_ = result[2].str()[0] - '0';
        if (result[3].length() > 0)
        {
            auto mode = result[3].str();
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
        if (result[4].length() > 0)
        {
            extensions_ = result[4].str();
        }
        if (result[5].length() > 0)
        {
            if (result[6].length() > 0)
                bass_note_step_ = result[6].str()[0] == 'b' ? flat_sharp::FLAT : flat_sharp::SHARP;
            assert(result[7].length() == 1);
            bass_note_ = result[7].str()[0] - '0';
        }
        if (result[8].length() > 0)
        {
            assert(result[9].length() == 1 || result[9].length() == 2);
            bool dot = result[9].length() == 2;
            assert((dot && result[9].str()[1] == '.') || !dot);
            switch (result[9].str()[0])
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
    CHUCHO_DEBUG_L("Found " << *this);
    return *this;
}

chord& chord::step(flat_sharp fs)
{
    step_ = fs;
    return *this;
}

std::string chord::to_user_input() const
{
    if (mode_ == type::UNDEFINED)
        return std::string();
    std::ostringstream out;
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
