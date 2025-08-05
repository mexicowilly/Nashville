#include "chord.hpp"
#include "chucho/log.hpp"
#include <stdexcept>
#include <regex>
#include <cassert>
#include <sstream>

namespace
{

struct memento
{
    memento(nashville::model::chord& c)
        : number_(c.number()),
          mode_(c.mode()),
          step_(c.step()),
          bass_note_(c.bass_note()),
          bass_note_step_(c.bass_note_step()),
          extensions_(c.extensions()),
          is_staccato_(c.is_staccato()),
          is_diamond_(c.is_diamond()),
          duration_(c.duration()),
          is_tied_(c.is_tied())
    {
    }

    unsigned number_;
    nashville::model::chord::type mode_;
    std::optional<nashville::model::chord::flat_sharp> step_;
    std::optional<unsigned> bass_note_;
    std::optional<nashville::model::chord::flat_sharp> bass_note_step_;
    std::string extensions_;
    bool is_staccato_;
    bool is_diamond_;
    std::optional<nashville::model::chord::time> duration_;
    bool is_tied_;
};

}

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
    memento saved(*this);
    this->reset();
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
        number_ = saved.number_;
        mode_ = saved.mode_;
        step_ = saved.step_;
        bass_note_ = saved.bass_note_;
        bass_note_step_ = saved.bass_note_step_;
        extensions_ = saved.extensions_;
        is_staccato_ = saved.is_staccato_;
        is_diamond_ = saved.is_diamond_;
        duration_ = saved.duration_;
        is_tied_ = saved.is_tied_;
        throw std::invalid_argument("'" + usr + "' is not a valid chord description");
    }
    CHUCHO_DEBUG_L("Found " << *this);
}

chord& chord::reset()
{
    number_ = 1;
    mode_ = type::MAJOR;
    step_.reset();
    bass_note_.reset();
    bass_note_step_.reset();
    extensions_.clear();
    is_staccato_ = false;
    is_diamond_ = false;
    duration_.reset();
    is_tied_ = false;
    return *this;
}

std::string chord::to_user_input() const
{
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
        out << *bass_note_;
    if (bass_note_step_)
        out << (*bass_note_step_ == flat_sharp::FLAT ? 'b' : '#');
    if (duration_)
    {
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
