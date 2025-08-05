#pragma once

#include "chucho/loggable.hpp"
#include <optional>
#include <string>
#include <ostream>
#include <QObject>

namespace nashville::model
{

class chord : public QObject, chucho::loggable<chord>
{
    Q_OBJECT

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

    chord();
    chord(unsigned number, type md = type::MAJOR);

    friend std::ostream& operator<< (std::ostream& out, const chord& c);
    bool operator== (const chord& other) const;

    std::optional<unsigned> bass_note() const;
    chord& bass_note(unsigned bn);
    std::optional<flat_sharp> bass_note_step() const;
    chord& bass_note_step(flat_sharp fs);
    std::optional<chord::time> duration() const;
    chord& duration(chord::time dur);
    const std::string& extensions() const;
    chord& extensions(const std::string& ext);
    bool is_diamond() const;
    chord& is_diamond(bool state);
    bool is_staccato() const;
    chord& is_staccato(bool state);
    bool is_tied() const;
    chord& is_tied(bool state);
    type mode() const;
    chord& mode(type m);
    unsigned number() const;
    chord& number(unsigned num);
    void parse_user_input(const std::string& usr);
    chord& reset();
    std::optional<flat_sharp> step() const;
    chord& step(flat_sharp fs);
    std::string to_user_input() const;

signals:
    void changed() const;

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
};

inline chord::chord()
{
    reset();
}

inline chord::chord(unsigned number, type md)
    : number_(number), mode_(md)
{
}

inline std::optional<unsigned> chord::bass_note() const
{
    return bass_note_;
}

inline chord& chord::bass_note(unsigned bn)
{
    bass_note_ = bn;
    return *this;
}

inline std::optional<chord::flat_sharp> chord::bass_note_step() const
{
    return bass_note_step_;
}

inline chord& chord::bass_note_step(chord::flat_sharp fs)
{
    bass_note_step_ = fs;
    return *this;
}

inline std::optional<chord::time> chord::duration() const
{
    return duration_;
}

inline chord& chord::duration(chord::time dur)
{
    duration_ = dur;
    return *this;
}

inline const std::string& chord::extensions() const
{
    return extensions_;
}

inline chord& chord::extensions(const std::string& ext)
{
    extensions_ = ext;
    return *this;
}

inline bool chord::is_diamond() const
{
    return is_diamond_;
}

inline chord& chord::is_diamond(bool state)
{
    is_diamond_ = state;
    return *this;
}

inline bool chord::is_staccato() const
{
    return is_staccato_;
}

inline chord& chord::is_staccato(bool state)
{
    is_staccato_ = state;
    return *this;
}

inline bool chord::is_tied() const
{
    return is_tied_;
}

inline chord& chord::is_tied(bool state)
{
    is_tied_ = state;
    return *this;
}

inline chord::type chord::mode() const
{
    return mode_;
}

inline chord& chord::mode(chord::type m)
{
    mode_ = m;
    return *this;
}

inline unsigned chord::number() const
{
    return number_;
}

inline chord& chord::number(unsigned num)
{
    number_ = num;
    return *this;
}

inline std::optional<chord::flat_sharp> chord::step() const
{
    return step_;
}

inline chord& chord::step(flat_sharp fs)
{
    step_ = fs;
    return *this;
}

}
