#include "chord.hpp"
#include "chucho/log.hpp"
#include <stdexcept>
#include <regex>

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
        default:
            out << " ";
            break;
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
    out << "}";
    return out;
}

void chord::parse_user_input(const std::string& usr)
{
    CHUCHO_DEBUG_L("Parsing user input: ''" << usr << "'");
    if (usr.empty())
        throw std::invalid_argument("The chord description cannot be empty");
    auto re = std::regex("([b#])?([1-7])(-|dim|\\+)?([^\\/]+)?(\\/([b#])?([1-7]))?");
    std::smatch result;
    if (std::regex_match(usr, result, re))
    {
        if (result[1].length() > 0)
        {
            if (result[1].str()[0] == 'b')
                step_ = flat_sharp::FLAT;
            else
                step_ = flat_sharp::SHARP;
        }
        number_ = result[2].str()[0] - '0';
        if (result[3].length() > 0)
        {
            auto mode = result[3].str();
            if (mode == "-")
                mode_ = type::MINOR;
            else if (mode == "dim")
                mode_ = type::DIMINISHED;
            else
                mode_ = type::AUGMENTED;
        }
        if (result[4].length() > 0)
        {
            extensions_ = result[4].str();
        }
        if (result[5].length() > 0)
        {
            if (result[6].length() > 0)
            {
                if (result[6].str()[0] == 'b')
                    bass_note_step_ = flat_sharp::FLAT;
                else
                    bass_note_step_ = flat_sharp::SHARP;

            }
            bass_note_ = result[7].str()[0] - '0';
        }
    }
    else
    {
        throw std::invalid_argument("'" + usr + "' is not a valid chord description");
    }
    CHUCHO_DEBUG_L("Found " << *this);
}

}
