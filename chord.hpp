#pragma once

#include <optional>
#include <string>

namespace nashville::model
{

class chord
{
public:
    enum class major_minor
    {
        MAJOR,
        MINOR
    };

    enum class flat_sharp
    {
        FLAT,
        SHARP
    };

    chord(unsigned number, major_minor md = major_minor::MAJOR);

    std::optional<unsigned> bass_note() const;
    void bass_note(unsigned bn);
    const std::string& extensions() const;
    void extensions(const std::string& ext);
    major_minor mode() const;
    unsigned number() const;
    std::optional<flat_sharp> step() const;
    void step(flat_sharp fs);

private:
    unsigned number_;
    major_minor mode_;
    std::optional<flat_sharp> step_;
    std::optional<unsigned> bass_note_;
    std::string extensions_;
};

inline chord::chord(unsigned number, major_minor md)
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

inline const std::string& chord::extensions() const
{
    return extensions_;
}

void chord::extensions(const std::string& ext)
{
    extensions_ = ext;
}

inline chord::major_minor chord::mode() const
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