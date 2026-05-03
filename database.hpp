#pragma once

#include <chucho/loggable.hpp>
#include "sqlite3.h"
#include <map>
#include <filesystem>
#include "model/song.hpp"
#include "model/playlist.hpp"

namespace nashville
{

class database : chucho::loggable<database>
{
public:
    database();
    database(const std::filesystem::path& file_name);
    ~database();

    bool in_memory() const;
    void insert_playlist(const model::playlist& pl);
    void insert_song(const model::song& s);
    void move_to_file(const std::filesystem::path& file_name);
    void remove_playlist(const std::string& pl);
    void remove_song(const std::string& s);
    model::playlist select_playlist(const std::string& name);
    std::vector<std::string> select_playlist_names();
    std::vector<std::string> select_song_names();
    model::song select_song(const std::string& name);

private:
    enum class statement
    {
        SELECT_CHORD,
        SELECT_CHORD_BY_ID,
        INSERT_CHORD,
        SELECT_BAR,
        INSERT_BAR,
        INSERT_BAR_CHORD,
        SELECT_CHORDS_BY_BAR,
        SELECT_TIME_SIGNATURE,
        INSERT_TIME_SIGNATURE,
        SELECT_SONG_ID,
        INSERT_SONG,
        SELECT_SONG_BARS,
        INSERT_SONG_BAR,
        INSERT_PLAYLIST,
        SELECT_PLAYLIST_SONGS,
        INSERT_PLAYLIST_SONG,
        SELECT_SONG_NAMES,
        SELECT_SONG,
        SELECT_PLAYLIST_NAMES,
        SELECT_PLAYLIST_ID,
        REMOVE_SONG,
        REMOVE_BAR,
        REMOVE_CHORD,
        SELECT_CHORD_NOT_IN_BAR,
        REMOVE_PLAYLIST
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

    class transaction
    {
    public:
        transaction(database& db);
        ~transaction();

        void commit();

    private:
        database& db_;
        bool is_committed_;
        bool is_active_;
    };

    std::uint64_t insert_bar(const model::bar& b);
    std::uint64_t insert_bar_chord(std::uint64_t bar_id, std::uint64_t chord_id, unsigned index);
    std::uint64_t insert_chord(const model::chord& c);
    std::uint64_t insert_song_bar(std::uint64_t song_id, std::uint64_t bar_id, unsigned index);
    void maybe_remove_chord(std::uint64_t bar_id, std::uint64_t chord_id);
    std::vector<model::bar> select_bars(std::uint64_t song_id);
    std::vector<model::chord> select_chords(std::uint64_t bar_id);
    std::uint64_t time_signature_id(const model::time_signature& ts);

    sqlite3* db_;
    // These are shared pointers so that the database will remain copyable.
    // It also facilitates creating the map with a bracketed initialization
    // list.
    std::map<statement, std::unique_ptr<prepared>> prepared_statements_;
};

inline bool database::in_memory() const
{
    return sqlite3_db_filename(db_, "main") == nullptr;
}

}
