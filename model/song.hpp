#pragma once

#include "loggable.hpp"
#include "bar.hpp"
#include "annotations.hpp"
#include <tuple>
#include <chrono>

namespace nashville::model
{

class song : public loggable
{
public:
    struct metadata
    {
        // These two are always set
        std::chrono::sys_time<std::chrono::milliseconds> creation_time;
        std::chrono::sys_time<std::chrono::milliseconds> modification_time;
        // Empty means none
        std::vector<std::string> authors;
        std::string original_performer;
        std::string original_album;
        std::string notes;
        // Optional because time_point has no value that means none
        std::optional<std::chrono::sys_days> original_album_release_date;
    };

    song();
    song(const std::string& nm);

    bar& add_bar();
    annotations& annotes();
    const annotations& annotes() const;
    const std::vector<bar>& bars() const;
    song& bars(const std::vector<bar>& bs);
    unsigned bars_per_line() const;
    song& bars_per_line(unsigned num);
    bool empty() const;
    const std::string& key() const;
    song& key(const std::string& k);
    song::metadata& meta();
    const song::metadata& meta() const;
    const std::string& name() const;
    song& name(const std::string& t);
    const std::tuple<unsigned, chord::time>& tempo() const;
    song& tempo(const std::tuple<unsigned, chord::time>& t);
    const time_signature& time_sig() const;
    song& time_sig(const time_signature& t);

private:
    std::vector<bar> bars_;
    std::string name_;
    std::string key_;
    time_signature time_signature_;
    std::tuple<unsigned, chord::time> tempo_;
    unsigned bars_per_line_ = 4;
    annotations annotations_;
    metadata metadata_;
};

inline bar& song::add_bar()
{
    return bars_.emplace_back(bar());
}

inline model::annotations& song::annotes()
{
    return annotations_;
}

inline const model::annotations& song::annotes() const
{
    return annotations_;
}

inline const std::vector<bar>& song::bars() const
{
    return bars_;
}

inline song& song::bars(const std::vector<bar>& bs)
{
    bars_ = bs;
    return *this;
}

inline unsigned song::bars_per_line() const
{
    return bars_per_line_;
}

inline bool song::empty() const
{
    return bars_.empty();
}

inline const std::string& song::key() const
{
    return key_;
}

inline song::metadata& song::meta()
{
    return metadata_;
}

inline const song::metadata& song::meta() const
{
    return metadata_;
}

inline const std::string& song::name() const
{
    return name_;
}

inline const std::tuple<unsigned, chord::time>& song::tempo() const
{
    return tempo_;
}

inline const time_signature& song::time_sig() const
{
    return time_signature_;
}

}
