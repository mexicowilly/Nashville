#pragma once

#include <chucho/loggable.hpp>
#include "sqlite3.h"
#include <map>

namespace nashville
{

class database : chucho::loggable<database>
{
public:
    database(const std::string& file_name);
    ~database();

private:
    enum class statement
    {
        SELECT_CHORD,
        INSERT_CHORD,
        SELECT_BAR,
        INSERT_BAR,
        INSERT_BAR_CHORD,
        INSERT_TIME_SIGNATURE,
        INSERT_SONG,
        INSERT_SONG_BAR,
        INSERT_PLAYLIST,
        INSERT_PLAYLIST_SONG
    };

    class prepared
    {
    public:
        prepared(sqlite3* db, const char* const sql);
        ~prepared();

        sqlite3_stmt* operator& () const { return stmt_; }
        void reset();

    private:
        sqlite3_stmt* stmt_;
    };

    sqlite3* db_;
    std::map<statement, prepared> prepared_statements_;
};

}
