#pragma once

#include "chucho/loggable.hpp"
#include <optional>
#include <string>
#include <ostream>

namespace nashville::model
{

class chord : chucho::loggable<chord>
{
public:
    enum class type
    {
        MAJOR,
        MINOR,
        DIMINISHED,
        AUGMENTED
    };

    enum class flat_sharp
    {
        FLAT,
        SHARP
    };

    enum class time
    {
        SIXTEENTH,
        EIGHTH,
        DOTTED_EIGHTH,
        QUARTER,
        DOTTED_QUARTER,
        HALF,
        DOTTED_HALF,
        WHOLE
    };

    chord() = default;
    chord(unsigned number, type md = type::MAJOR);
    chord(const chord& other) = default;

    chord& operator= (const chord& other) = default;
    friend std::ostream& operator<< (std::ostream& out, const chord& c);

    std::optional<unsigned> bass_note() const;
    void bass_note(unsigned bn);
    std::optional<chord::time> duration() const;
    void duration(chord::time dur);
    const std::string& extensions() const;
    void extensions(const std::string& ext);
    bool is_diamond() const;
    void is_diamond(bool state);
    bool is_staccato() const;
    void is_staccato(bool state);
    type mode() const;
    unsigned number() const;
    void parse_user_input(const std::string& usr);
    std::optional<flat_sharp> step() const;
    void step(flat_sharp fs);

private:
    unsigned number_ = 1;
    type mode_ = type::MAJOR;
    std::optional<flat_sharp> step_;
    std::optional<unsigned> bass_note_;
    std::optional<flat_sharp> bass_note_step_;
    std::string extensions_;
    bool is_staccato_ = false;
    bool is_diamond_ = false;
    std::optional<time> duration_;
};

inline chord::chord(unsigned number, type md)
    : number_(number), mode_(md)
{
}

inline std::optional<unsigned> chord::bass_note() const
{
    return bass_note_;
}

inline void chord::bass_note(unsigned bn)
{
    bass_note_ = bn;
}

inline std::optional<chord::time> chord::duration() const
{
    return duration_;
}

inline void chord::duration(chord::time dur)
{
    duration_ = dur;
}

inline const std::string& chord::extensions() const
{
    return extensions_;
}

inline void chord::extensions(const std::string& ext)
{
    extensions_ = ext;
}

inline bool chord::is_diamond() const
{
    return is_diamond_;
}

inline void chord::is_diamond(bool state)
{
    is_diamond_ = state;
}

inline bool chord::is_staccato() const
{
    return is_staccato_;
}

inline void chord::is_staccato(bool state)
{
    is_staccato_ = state;
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

inline void chord::step(flat_sharp fs)
{
    step_ = fs;
}

}
