#pragma once

#include <optional>
#include <string>
#include <ostream>
#include "loggable.hpp"

namespace nashville::model
{

class chord : public loggable
{
public:
    enum class type
    {
        MAJOR,
        MINOR,
        DIMINISHED,
        AUGMENTED,
        UNDEFINED
    };

    enum class flat_sharp
    {
        FLAT,
        SHARP
    };

    enum class time
    {
        SIXTEENTH = 16,
        EIGHTH = 8,
        DOTTED_EIGHTH = 12,
        QUARTER = 4,
        DOTTED_QUARTER = 6,
        HALF = 2,
        DOTTED_HALF = 3,
        WHOLE = 1
    };

    static constexpr unsigned REST = 0;

    chord();

    bool operator== (const chord& other) const;

    std::optional<unsigned> bass_note() const;
    chord& bass_note(unsigned bn);
    std::optional<flat_sharp> bass_note_step() const;
    chord& bass_note_step(flat_sharp fs);
    chord& clear();
    std::optional<chord::time> duration() const;
    chord& duration(chord::time dur);
    const std::string& extensions() const;
    chord& extensions(const std::string& ext);
    bool is_diamond() const;
    chord& is_diamond(bool state);
    bool is_pushed() const;
    chord& is_pushed(bool state);
    bool is_staccato() const;
    chord& is_staccato(bool state);
    bool is_tied() const;
    chord& is_tied(bool state);
    type mode() const;
    chord& mode(type m);
    unsigned number() const;
    chord& number(unsigned num);
    chord& parse_user_input(const std::string& usr);
    std::optional<flat_sharp> step() const;
    chord& step(flat_sharp fs);
    std::string to_string() const;
    std::string to_user_input() const;

private:
    unsigned number_;
    type mode_;
    std::optional<flat_sharp> step_;
    std::optional<unsigned> bass_note_;
    std::optional<flat_sharp> bass_note_step_;
    std::string extensions_;
    bool is_staccato_;
    bool is_diamond_;
    std::optional<time> duration_;
    bool is_tied_;
    bool is_pushed_;
};

inline std::optional<unsigned> chord::bass_note() const
{
    return bass_note_;
}

inline std::optional<chord::flat_sharp> chord::bass_note_step() const
{
    return bass_note_step_;
}

inline chord& chord::clear()
{
    *this = chord();
    return *this;
}

inline std::optional<chord::time> chord::duration() const
{
    return duration_;
}

inline const std::string& chord::extensions() const
{
    return extensions_;
}

inline bool chord::is_diamond() const
{
    return is_diamond_;
}

inline bool chord::is_pushed() const
{
    return is_pushed_;
}

inline bool chord::is_staccato() const
{
    return is_staccato_;
}

inline bool chord::is_tied() const
{
    return is_tied_;
}

inline chord::type chord::mode() const
{
    return mode_;
}

inline unsigned chord::number() const
{
    return number_;
}

inline std::optional<chord::flat_sharp> chord::step() const
{
    return step_;
}

}
