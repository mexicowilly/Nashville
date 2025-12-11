#pragma once

#include <chucho/loggable.hpp>
#include "sqlite3.h"
#include <map>
#include "model/song.hpp"

namespace nashville
{

class database : chucho::loggable<database>
{
public:
    database(const std::string& file_name);
    ~database();

    void insert_song(const model::song& s);

private:
    enum class statement
    {
        SELECT_CHORD,
        INSERT_CHORD,
        SELECT_BAR,
        INSERT_BAR,
        SELECT_BAR_CHORD,
        INSERT_BAR_CHORD,
        SELECT_TIME_SIGNATURE,
        INSERT_TIME_SIGNATURE,
        SELECT_SONG,
        INSERT_SONG,
        SELECT_SONG_BAR,
        INSERT_SONG_BAR,
        SELECT_PLAYLIST,
        INSERT_PLAYLIST,
        SELECT_PLAYLIST_SONG,
        INSERT_PLAYLIST_SONG
    };

    class prepared
    {
    public:
        prepared(sqlite3* db, const char* const sql);
        ~prepared();

        sqlite3_stmt* ptr() const { return stmt_; }
        void reset();

    private:
        sqlite3_stmt* stmt_;
    };

    std::uint64_t insert_bar(const model::bar& b);
    std::uint64_t insert_chord(const model::chord& c);
    std::uint64_t time_signature_id(const model::time_signature& ts);

    sqlite3* db_;
    // These are shared pointers so that the database will remain copyable.
    // It also facilitates creating the map with a bracketed initialization
    // list.
    std::map<statement, std::shared_ptr<prepared>> prepared_statements_;
};

}
