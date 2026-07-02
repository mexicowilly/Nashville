#pragma once

#include "loggable.hpp"
#include "sqlite3.h"
#include "db_ids.hpp"
#include <map>
#include <optional>
#include <filesystem>
#include "model/song.hpp"
#include "model/playlist.hpp"

namespace nashville
{

// A song loaded from the database, paired with the row identity the caller
// keeps for the rest of the session.
using stored_song = stored<song_id, model::song>;

// A song's name plus its descriptive metadata, with no chart data.  Returned
// by song_summaries() so the UI can sort the song list by any field without
// loading every song in full.  All fields are stored as text in the database
// (dates as ISO 8601, which sorts chronologically as a string); NULLs come
// back as empty strings.
struct song_summary
{
    std::string name;
    std::string authors;       // CSV
    std::string performer;
    std::string album;
    std::string release_date;  // ISO 8601
    std::string notes;
    std::string created;       // ISO 8601
    std::string modified;      // ISO 8601
};

class database : loggable
{
public:
    static constexpr int CURRENT_VERSION = 1;
    static constexpr int MAX_SUPPORTED_VERSION = 1;

    database();
    database(const std::filesystem::path& file_name);
    ~database();

    std::filesystem::path file_name() const;
    std::uint64_t file_version() const;
    bool in_memory() const;
    playlist_id insert_playlist(const model::playlist& pl);
    song_id insert_song(const model::song& s);
    std::vector<stored_song> last_open_songs();
    void last_open_songs(const std::vector<song_id>& opens);

    // The persisted song-list sort key (a stable token such as "name" or
    // "modified").  Reader defaults to "name" when nothing is stored; the
    // writer records the UI's current choice.  The sort itself is the UI's
    // job — this is only where the choice is remembered across reloads.
    std::string song_list_sort();
    void song_list_sort(const std::string& key);

    // Whether the song list sort is descending (the reader defaults to false
    // = ascending).  A separate UI preference, persisted alongside the key.
    bool song_list_sort_descending();
    void song_list_sort_descending(bool descending);

    // Every song's name and descriptive metadata, unsorted.  Lets the UI sort
    // the song list by any field without loading songs in full.
    std::vector<song_summary> song_summaries();
    void update_song(song_id id, const model::song& s);
    void move_to_file(const std::filesystem::path& file_name);
    void open_file(const std::filesystem::path& file_name);
    void remove_playlist(const std::string& pl);
    void remove_song(const std::string& s);
    void rename_playlist(playlist_id id, const std::string& new_name);
    std::optional<playlist_id> playlist_id_of(const std::string& name);
    model::playlist select_playlist(const std::string& name);
    std::vector<std::string> select_playlist_names();
    std::vector<std::string> select_song_names();
    stored_song select_song(const std::string& name);

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
        REMOVE_PLAYLIST,
        // Annotation persistence: text boxes and straight-line
        // connectors live in their own tables that cascade-delete on
        // song removal (ON DELETE CASCADE in the schema).  See the
        // INSERT_* / SELECT_SONG_* SQL constants in database.cpp.
        INSERT_TEXT_BOX,
        SELECT_SONG_TEXT_BOXES,
        INSERT_CONNECTOR,
        SELECT_SONG_CONNECTORS,
        UPDATE_SONG,
        RENAME_PLAYLIST,
        DELETE_SONG_TEXT_BOXES,
        DELETE_SONG_CONNECTORS
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

    void check_version();
    std::string error_msg(int rc) const;
    void bind_song_columns(sqlite3_stmt* raw, const model::song& s);
    void write_song_body(std::int64_t song_id, const model::song& s);
    std::uint64_t insert_bar(const model::bar& b);
    std::uint64_t insert_bar_chord(std::uint64_t bar_id, std::uint64_t chord_id, unsigned index);
    std::uint64_t insert_chord(const model::chord& c);
    std::uint64_t insert_song_bar(std::uint64_t song_id, std::uint64_t bar_id, unsigned index);
    void maybe_remove_chord(std::uint64_t bar_id, std::uint64_t chord_id);
    void remove_song_bars(std::uint64_t song_id);
    std::vector<model::bar> select_bars(std::uint64_t song_id);
    std::vector<model::chord> select_chords(std::uint64_t bar_id);
    std::uint64_t time_signature_id(const model::time_signature& ts);

    sqlite3* db_;
    std::map<statement, std::unique_ptr<prepared>> prepared_statements_;
};

inline bool database::in_memory() const
{
    auto fn = sqlite3_db_filename(db_, "main");
    return fn == nullptr || std::strlen(fn) == 0;
}

}
