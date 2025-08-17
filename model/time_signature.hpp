#pragma once

#include <string>

namespace nashville::model
{

class time_signature
{
public:
    enum class beat_type
    {
        EIGHTH,
        QUARTER,
        HALF
    };

    bool operator== (const time_signature& other) const;
    unsigned count() const;
    time_signature& count(unsigned num);
    beat_type kind() const;
    time_signature& kind(beat_type bt);
    void parse_user_input(const std::string& inp);

private:
    beat_type kind_ = beat_type::QUARTER;
    unsigned count_ = 4;
};

inline unsigned time_signature::count() const
{
    return count_;
}

inline time_signature& time_signature::count(unsigned num)
{
    count_ = num;
    return *this;
}

inline time_signature::beat_type time_signature::kind() const
{
    return kind_;
}

inline time_signature& time_signature::kind(beat_type bt)
{
    kind_ = bt;
    return *this;
}

}
