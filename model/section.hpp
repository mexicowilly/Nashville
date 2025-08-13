#pragma once

#include <chucho/loggable.hpp>
#include "bar.hpp"

namespace nashville::model
{

class section : public chucho::loggable<section>
{
public:
    bar& add_bar();
    const std::vector<bar> bars() const;
    const std::string& name() const;
    section& name(const std::string& nm);

private:
    std::vector<bar> bars_;
    std::string name_;
};

inline bar& section::add_bar()
{
    return bars_.emplace_back(bar());
}

inline const std::vector<bar> section::bars() const
{
    return bars_;
}

inline const std::string& section::name() const
{
    return name_;
}

inline section& section::name(const std::string& nm)
{
    name_ = nm;
    return *this;
}

}
