#include "chord.hpp"
#include "chucho/log.hpp"
#include <stdexcept>
#include <regex>
#include <cassert>

namespace nashville::model
{

std::ostream& operator<< (std::ostream& out, const chord& c)
{
    out << "chord:{";
    if (c.step_)
        out << (c.step_ == chord::flat_sharp::FLAT ? "flat " : "sharp ");
    out << c.number_;
    switch (c.mode_)
    {
        case chord::type::MINOR:
            out << " min ";
            break;
        case chord::type::DIMINISHED:
            out << " dim ";
            break;
        case chord::type::AUGMENTED:
            out << " aug ";
            break;
        default: ;
    }
    out << c.extensions_;
    if (c.bass_note_)
    {
        out << "/ ";
        if (c.bass_note_step_)
            out << (*c.bass_note_step_ == chord::flat_sharp::FLAT ? "flat " : "sharp ");
        out << *c.bass_note_;
    }
    if (c.is_staccato_ || c.is_diamond_)
    {
        out << " (";
        if (c.is_diamond_)
        {
            out << "d";
            if (c.is_staccato_)
                out << ",";
        }
        if (c.is_staccato_)
            out << "s";
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

void chord::parse_user_input(const std::string& usr)
{
    CHUCHO_DEBUG_L("Parsing user input: '" << usr << "'");
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
}

}
