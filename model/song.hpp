#pragma once

#include <chucho/loggable.hpp>
#include "section.hpp"

namespace nashville::model
{

class song : public chucho::loggable<song>
{
public:
    section& add_section(const std::string& name = std::string());
    const std::vector<section>& sections() const;
    bool empty() const;
    const std::string& key() const;
    song& key(const std::string k);
    unsigned tempo() const;
    song& tempo(unsigned t);
    const time_signature& time_sig() const;
    song& time_sig(const time_signature& t);
    const std::string& title() const;
    song& title(const std::string t);

private:
    std::vector<section> sections_;
    std::string title_;
    std::string key_;
    time_signature time_signature_;
    // The type of beat for the tempo is taken from the time signature
    unsigned tempo_;
};

inline section& song::add_section(const std::string& name)
{
    return sections_.emplace_back(section()).name(name);
}

inline bool song::empty() const
{
    return sections_.empty();
}

inline const std::string& song::key() const
{
    return key_;
}

inline const std::vector<section>& song::sections() const
{
    return sections_;
}

inline unsigned song::tempo() const
{
    return tempo_;
}

inline const time_signature& song::time_sig() const
{
    return time_signature_;
}

inline const std::string& song::title() const
{
    return title_;
}

}
