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
// loading every song in full (which would mean a select_bars() per row).
// model::song::metadata already excludes bars/annotations for exactly this
// reason, and already carries every field song_summaries() needs in its
// proper type (chrono timestamps, vector<string> authors, etc.) — so the
// summary just pairs a name with that same struct rather than restating
// each field as a string. song_summaries() parses the stored TEXT columns
// (ISO 8601 dates, CSV authors) through the same helpers select_song() uses,
// so callers get real types instead of re-parsing strings themselves.
struct song_summary
{
    std::string name;
    model::song::metadata meta;
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
    // allow_empty_bars must be set true only for a deliberate, user-confirmed
    // "delete all bars".  Left false (the default), the write refuses to
    // replace an existing non-empty bar set with an empty one — a last-line
    // data-loss guard (see write_song_body).
    void update_song(song_id id, const model::song& s, bool allow_empty_bars = false);
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

    // Scoped write transaction.
    //
    // Nesting is tracked by an explicit depth counter on the database, NOT by
    // asking SQLite what state it is in.  The previous implementation used
    // sqlite3_txn_state() == SQLITE_TXN_NONE to decide whether to issue BEGIN,
    // which is unsound in two directions: a SELECT left mid-iteration holds an
    // open read transaction, so txn_state reports READ and a genuine top-level
    // write silently became a no-op wrapper (losing atomicity — the individual
    // statements then autocommitted one at a time, so a failure part-way
    // through left the database half-written with nothing to roll back); and
    // conversely BEGIN is deferred, so immediately after a real BEGIN with no
    // statement executed yet txn_state still reports NONE, and a genuinely
    // nested transaction would issue a second BEGIN and fail.
    //
    // With a depth counter the outermost scope owns the physical BEGIN and is
    // the only one that commits.  Inner scopes join it.  If any scope — inner
    // or outer — is destroyed without commit() (an exception, an early return),
    // the whole transaction is marked abandoned and the outermost scope rolls
    // back rather than committing a partial write.
    class transaction
    {
    public:
        explicit transaction(database& db);
        ~transaction();

        transaction(const transaction&) = delete;
        transaction& operator=(const transaction&) = delete;

        void commit();

    private:
        database& db_;
        bool owns_begin_;   // this scope issued the physical BEGIN
        bool resolved_;     // commit() ran to a decision on this scope
    };

    // Borrows a prepared statement for the duration of a scope, resetting it
    // both on entry and on exit.  The exit reset is the point: a statement
    // left mid-row (a SELECT abandoned early, or an INSERT ... RETURNING whose
    // single row was read but never stepped past) keeps a read transaction
    // open on the connection.  That no longer breaks transaction nesting now
    // that depth is tracked explicitly, but it still pins the connection's
    // snapshot and blocks COMMIT, so it is worth not doing.
    class stmt_guard
    {
    public:
        explicit stmt_guard(prepared& p) : p_(p) { p_.reset(); }
        ~stmt_guard() { p_.reset(); }

        stmt_guard(const stmt_guard&) = delete;
        stmt_guard& operator=(const stmt_guard&) = delete;

        sqlite3_stmt* ptr() const { return p_.ptr(); }

    private:
        prepared& p_;
    };

    void check_version();
    std::string error_msg(int rc) const;
    void bind_song_columns(sqlite3_stmt* raw, const model::song& s);
    void write_song_body(std::int64_t song_id, const model::song& s,
                         bool allow_empty_bars = false);
    // True iff the given song currently has at least one persisted bar row.
    // Used by write_song_body's data-loss guard to decide, from the actual
    // database state (not any in-memory snapshot), whether an incoming empty
    // bar set would wipe a real chart.
    bool song_has_bars(std::int64_t song_id);
    std::uint64_t insert_bar(const model::bar& b);
    std::uint64_t insert_bar_chord(std::uint64_t bar_id, std::uint64_t chord_id, unsigned index);
    std::uint64_t insert_chord(const model::chord& c);
    std::uint64_t insert_song_bar(std::uint64_t song_id, std::uint64_t bar_id, unsigned index);
    void maybe_remove_chord(std::uint64_t bar_id, std::uint64_t chord_id);
    void remove_song_bars(std::uint64_t song_id);
    std::vector<model::bar> select_bars(std::uint64_t song_id);
    std::vector<model::chord> select_chords(std::uint64_t bar_id);
    std::uint64_t time_signature_id(const model::time_signature& ts);

    // Reset every cached prepared statement.  Called before COMMIT and before
    // ROLLBACK: SQLite refuses to end a transaction while statements are still
    // in progress, and any statement left mid-row would otherwise survive into
    // the next transaction holding a stale read.
    void reset_all_statements();
    // Reset the statements and issue ROLLBACK, logging the outcome.  Used by
    // the transaction destructor and by commit() when the transaction has been
    // abandoned by an inner scope.
    void rollback_all();

    sqlite3* db_;
    std::map<statement, std::unique_ptr<prepared>> prepared_statements_;

    // Nesting depth of live transaction objects.  0 means no transaction is
    // open, so the next transaction constructed issues the physical BEGIN.
    int txn_depth_ = 0;
    // Set when any transaction scope is destroyed without a successful
    // commit().  The outermost scope reads this and rolls back instead of
    // committing, so a failed inner step can never be committed by its caller.
    bool txn_abandoned_ = false;
};

inline bool database::in_memory() const
{
    auto fn = sqlite3_db_filename(db_, "main");
    return fn == nullptr || std::strlen(fn) == 0;
}

}
