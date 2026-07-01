#include "database.hpp"
#include "iso8601.hpp"
#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <set>
#include <sstream>
#include <system_error>
// Annotation binding/reading uses these directly; they're pulled in
// transitively via model/annotations.hpp, but listing them explicitly
// documents the dependency.
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QByteArray>

using namespace std::string_literals;

namespace
{

const char* schema = R"(
PRAGMA encoding = 'UTF-8';
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS metadata
(
    version INTEGER,
    -- This is a comma-separated list of song ids
    last_open_song_ids TEXT,
    -- Token naming the field the song list is sorted by, e.g. "name",
    -- "authors", "modified".  Purely a UI preference; the sort itself is
    -- performed in the UI, this is just where the choice is remembered.
    song_list_sort TEXT,
    song_list_sort_desc BOOLEAN
);

CREATE TABLE IF NOT EXISTS chord
(
    id INTEGER PRIMARY KEY,
    number INTEGER,
    mode INTEGER,
    flat_sharp BOOLEAN,
    bass_note INTEGER,
    bass_note_step BOOLEAN,
    extensions TEXT,
    is_staccato BOOLEAN,
    is_diamond BOOLEAN,
    duration INTEGER,
    is_tied BOOLEAN,
    is_pushed BOOLEAN
);

CREATE TABLE IF NOT EXISTS bar
(
    id INTEGER PRIMARY KEY,
    time_sig_id INTEGER,
    is_eol BOOLEAN,
    section TEXT,
    repeat INTEGER,
    voltas TEXT,
    number_of_beats INTEGER,
    modulation TEXT,
    FOREIGN KEY(time_sig_id) REFERENCES time_signature(id)
);

CREATE TABLE IF NOT EXISTS time_signature
(
    id INTEGER PRIMARY KEY,
    beat_type INTEGER CHECK(beat_type = 8 OR beat_type = 4 OR beat_type = 2),
    count INTEGER
);

CREATE TABLE IF NOT EXISTS bar_chords
(
    id INTEGER PRIMARY KEY,
    chord_id INTEGER,
    bar_id INTEGER,
    chord_index INTEGER,
    FOREIGN KEY(chord_id) REFERENCES chord(id) ON DELETE CASCADE,
    FOREIGN KEY(bar_id) REFERENCES bar(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS bar_id_index ON bar_chords(bar_id, chord_index);

CREATE TABLE IF NOT EXISTS song
(
    id INTEGER PRIMARY KEY,
    name TEXT UNIQUE,
    key TEXT,
    time_sig_id INTEGER,
    bars_per_line INTEGER,
    beats_per_minute INTEGER,
    beats_unit INTEGER,
    -- These are the metadata
    creation_time TEXT,                 -- ISO 8601
    modification_time TEXT,             -- ISO 8601
    authors TEXT, -- CSV list
    original_performer TEXT,
    original_album TEXT,
    notes TEXT,
    original_album_release_date TEXT,   -- ISO 8601
    -- This is for the UI
    margin_width INTEGER,
    FOREIGN KEY(time_sig_id) REFERENCES time_signature(id)
);

CREATE TABLE IF NOT EXISTS song_bars
(
    id INTEGER PRIMARY KEY,
    bar_id INTEGER,
    song_id INTEGER,
    bar_index INTEGER,
    FOREIGN KEY(bar_id) REFERENCES bar(id) ON DELETE CASCADE,
    FOREIGN KEY(song_id) REFERENCES song(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS song_id_index ON song_bars(song_id, bar_index);

CREATE TABLE IF NOT EXISTS playlist
(
    id INTEGER PRIMARY KEY,
    name TEXT UNIQUE
);

CREATE TABLE IF NOT EXISTS playlist_songs
(
    id INTEGER PRIMARY KEY,
    song_id INTEGER,
    playlist_id INTEGER,
    song_index INTEGER,
    FOREIGN KEY(song_id) REFERENCES song(id) ON DELETE CASCADE,
    FOREIGN KEY(playlist_id) REFERENCES playlist(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS playlist_id_index ON playlist_songs(playlist_id, song_index);

CREATE TABLE IF NOT EXISTS text_box
(
    id INTEGER PRIMARY KEY,
    song_id INTEGER NOT NULL,
    x REAL NOT NULL,
    y REAL NOT NULL,
    w REAL NOT NULL,
    h REAL NOT NULL,
    text TEXT NOT NULL DEFAULT '',
    FOREIGN KEY(song_id) REFERENCES song(id) ON DELETE CASCADE
);

CREATE INDEX IF NOT EXISTS text_box_song_id_index ON text_box(song_id);

CREATE TABLE IF NOT EXISTS connector
(
    id INTEGER PRIMARY KEY,
    song_id INTEGER NOT NULL,
    -- Free-endpoint coordinates.  Used only when the corresponding
    -- start_text_box_id / end_text_box_id is NULL; otherwise these
    -- still get persisted (as the last-known resolved position) so
    -- a defensive read can fall back if the referenced text box has
    -- somehow gone missing, but they're authoritative only for
    -- free endpoints.
    x1 REAL NOT NULL,
    y1 REAL NOT NULL,
    x2 REAL NOT NULL,
    y2 REAL NOT NULL,
    arrow_start INTEGER NOT NULL DEFAULT 0,
    arrow_end   INTEGER NOT NULL DEFAULT 1,
    -- Anchor references.  NULL means "this endpoint is free; use the
    -- xN/yN coordinates."  NOT NULL means "glued to this text box's
    -- nth anchor (0..7)."  The FK uses ON DELETE SET NULL so that
    -- when a text box is removed at the database level (e.g. via
    -- some path that bypasses the in-memory annotations layer), the
    -- connector survives with the endpoint converted to free.
    start_text_box_id INTEGER,
    start_anchor_index INTEGER,
    end_text_box_id INTEGER,
    end_anchor_index INTEGER,
    FOREIGN KEY(song_id) REFERENCES song(id) ON DELETE CASCADE,
    FOREIGN KEY(start_text_box_id) REFERENCES text_box(id) ON DELETE SET NULL,
    FOREIGN KEY(end_text_box_id)   REFERENCES text_box(id) ON DELETE SET NULL
);

CREATE INDEX IF NOT EXISTS connector_song_id_index ON connector(song_id);
)";

const char* SELECT_CHORD_SQL = R"(
SELECT id FROM chord WHERE
    number = ?1 AND
    mode = ?2 AND
    flat_sharp IS ?3 AND
    bass_note IS ?4 AND
    bass_note_step IS ?5 AND
    extensions = ?6 AND
    is_staccato = ?7 AND
    is_diamond = ?8 AND
    duration IS ?9 AND
    is_tied = ?10 AND
    is_pushed = ?11;
)";

const char* SELECT_CHORDS_BY_BAR_SQL = R"(
SELECT chord_id FROM bar_chords WHERE bar_id = ?1 ORDER BY chord_index;
)";

const char* SELECT_CHORD_BY_ID_SQL = R"(
SELECT * FROM chord WHERE id = ?1;
)";

const char* INSERT_CHORD_SQL = R"(
INSERT INTO chord (number,
                   mode,
                   flat_sharp,
                   bass_note,
                   bass_note_step,
                   extensions,
                   is_staccato,
                   is_diamond,
                   duration,
                   is_tied,
                   is_pushed)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)
RETURNING id;
)";

const char* SELECT_BAR_SQL = R"(
SELECT bar.is_eol,
       bar.section,
       time_signature.beat_type,
       time_signature.count,
       bar.repeat,
       bar.voltas,
       bar.number_of_beats,
       bar.modulation
FROM bar
LEFT JOIN time_signature ON time_signature.id = bar.time_sig_id
WHERE bar.id = ?1;
)";

const char* INSERT_BAR_SQL = R"(
INSERT INTO bar (time_sig_id, is_eol, section, repeat, voltas, number_of_beats, modulation)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
RETURNING id;
)";

const char* SELECT_TIME_SIGNATURE_SQL = R"(
SELECT * FROM time_signature WHERE (beat_type = ?1 AND count = ?2);
)";

const char* INSERT_TIME_SIGNATURE_SQL = R"(
INSERT INTO time_signature (beat_type, count)
VALUES (?1, ?2)
RETURNING id;
)";

const char* INSERT_BAR_CHORD_SQL = R"(
INSERT INTO bar_chords (chord_id, bar_id, chord_index)
VALUES (?1, ?2, ?3)
RETURNING id;
)";

const char* SELECT_SONG_SQL = R"(
SELECT song.id,
       song.key,
       song.bars_per_line,
       song.beats_per_minute,
       song.beats_unit,
       time_signature.beat_type,
       time_signature.count,
       song.creation_time,
       song.modification_time,
       song.authors,
       song.original_performer,
       song.original_album,
       song.notes,
       song.original_album_release_date,
       song.margin_width
FROM song
LEFT JOIN time_signature ON time_signature.id = song.time_sig_id
WHERE song.name = ?1;
)";

const char* SELECT_SONG_ID_SQL = R"(
SELECT id FROM song WHERE name = ?1;
)";

const char* SELECT_SONG_NAMES_SQL = R"(
SELECT name FROM song;
)";

const char* INSERT_SONG_SQL = R"(
INSERT INTO song (name,
                  key,
                  time_sig_id,
                  bars_per_line,
                  beats_per_minute,
                  beats_unit,
                  creation_time,
                  modification_time,
                  authors,
                  original_performer,
                  original_album,
                  notes,
                  original_album_release_date,
                  margin_width)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11, ?12, ?13, ?14)
RETURNING id;
)";

// Parameters ?1..?14 match INSERT_SONG exactly so the column binding can be
// shared (bind_song_columns); ?15 is the row identity for the WHERE clause.
const char* UPDATE_SONG_SQL = R"(
UPDATE song SET
    name = ?1,
    key = ?2,
    time_sig_id = ?3,
    bars_per_line = ?4,
    beats_per_minute = ?5,
    beats_unit = ?6,
    creation_time = ?7,
    modification_time = ?8,
    authors = ?9,
    original_performer = ?10,
    original_album = ?11,
    notes = ?12,
    original_album_release_date = ?13,
    margin_width = ?14
WHERE id = ?15;
)";

const char* SELECT_SONG_BARS_SQL = R"(
SELECT bar_id FROM song_bars WHERE song_id = ?1 ORDER BY bar_index;
)";

const char* INSERT_SONG_BAR_SQL = R"(
INSERT INTO song_bars (bar_id, song_id, bar_index)
VALUES (?1, ?2, ?3)
RETURNING id;
)";

const char* SELECT_PLAYLIST_NAMES_SQL = R"(
SELECT name FROM playlist;
)";

const char* INSERT_PLAYLIST_SQL = R"(
INSERT INTO playlist (name) VALUES (?1) RETURNING id;
)";

const char* SELECT_PLAYLIST_SONGS_SQL = R"(
SELECT song.name
FROM playlist_songs
JOIN song ON playlist_songs.song_id = song.id
WHERE playlist_songs.playlist_id = ?1
ORDER BY playlist_songs.song_index;
)";

const char* INSERT_PLAYLIST_SONG_SQL = R"(
INSERT INTO playlist_songs (song_id, playlist_id, song_index)
VALUES (?1, ?2, ?3);
)";

const char* SELECT_PLAYLIST_ID_SQL = R"(
SELECT id FROM playlist WHERE name = ?1;
)";

const char* SELECT_CHORD_NOT_IN_BAR_SQL = R"(
SELECT id FROM bar_chords WHERE (chord_id = ?1 AND bar_id != ?2);
)";

const char* REMOVE_CHORD_SQL = R"(
DELETE FROM chord WHERE id = ?1;
)";

const char* REMOVE_SONG_SQL = R"(
DELETE FROM song WHERE id = ?1;
)";

const char* REMOVE_BAR_SQL = R"(
DELETE FROM bar WHERE id = ?1;
)";

const char* REMOVE_PLAYLIST_SQL = R"(
DELETE FROM playlist WHERE name = ?1;
)";

const char* RENAME_PLAYLIST_SQL = R"(
UPDATE playlist SET name=?1 WHERE id = ?2;
)";

// --- Annotation persistence -------------------------------------------------
// One row per text box / connector, FK'd to song with ON DELETE CASCADE.
// Coordinates are stored in song-local space (see annotation_layer for
// the convention) as REAL.  Connectors' arrow_* flags are stored as
// INTEGER booleans (0/1), same convention as bar.is_eol etc.

const char* INSERT_TEXT_BOX_SQL = R"(
INSERT INTO text_box (song_id, x, y, w, h, text)
VALUES (?1, ?2, ?3, ?4, ?5, ?6)
RETURNING id;
)";

const char* SELECT_SONG_TEXT_BOXES_SQL = R"(
SELECT id, x, y, w, h, text FROM text_box WHERE song_id = ?1 ORDER BY id;
)";

const char* INSERT_CONNECTOR_SQL = R"(
INSERT INTO connector
    (song_id, x1, y1, x2, y2, arrow_start, arrow_end,
     start_text_box_id, start_anchor_index,
     end_text_box_id,   end_anchor_index)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)
RETURNING id;
)";

const char* SELECT_SONG_CONNECTORS_SQL = R"(
SELECT id, x1, y1, x2, y2, arrow_start, arrow_end,
       start_text_box_id, start_anchor_index,
       end_text_box_id,   end_anchor_index
FROM connector
WHERE song_id = ?1 ORDER BY id;
)";

const char* DELETE_SONG_TEXT_BOXES_SQL = R"(
DELETE FROM text_box WHERE song_id = ?1;
)";

const char* DELETE_SONG_CONNECTORS_SQL = R"(
DELETE FROM connector WHERE song_id = ?1;
)";

}

namespace nashville
{

database::prepared::prepared(sqlite3* db, const char* const sql)
{
    auto rc = sqlite3_prepare_v2(db,
                                 sql,
                                 -1,
                                 &stmt_,
                                 NULL);
    if (rc != SQLITE_OK)
    {
        // THIS IS FATAL
        throw std::invalid_argument("Error preparing statement: '"s + sql + "': " + sqlite3_errmsg(db));
    }
}

database::prepared::~prepared()
{
    sqlite3_finalize(stmt_);
}

void database::prepared::reset()
{
    sqlite3_clear_bindings(stmt_);
    sqlite3_reset(stmt_);
}

database::transaction::transaction(database& db)
    : db_(db),
      is_committed_(false),
      is_active_(false)
{
    if (sqlite3_txn_state(db.db_, nullptr) == SQLITE_TXN_NONE)
    {
        char* err = nullptr;
        if (sqlite3_exec(db.db_, "BEGIN;", nullptr, nullptr, &err) != SQLITE_OK)
        {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("Could not begin transaction: " + msg);
        }
        is_active_ = true;
        db.lgr()->debug("Created active transaction");
    }
    else
    {
        db.lgr()->debug("Created inactive transaction");
    }
}

database::transaction::~transaction()
{
    if (is_active_ && !is_committed_)
    {
        sqlite3_exec(db_.db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        db_.lgr()->debug("Rolled back active transaction");
    }
}

void database::transaction::commit()
{
    if (is_active_)
    {
        for (auto& p : db_.prepared_statements_)
            p.second->reset();
        char* err = nullptr;
        if (sqlite3_exec(db_.db_, "COMMIT;", nullptr, nullptr, &err) != SQLITE_OK)
        {
            std::string msg = err ? err : "unknown error";
            sqlite3_free(err);
            throw std::runtime_error("Could not commit transaction: " + msg);
        }
        is_committed_ = true;
        db_.lgr()->debug("Committed active transaction");
    }
}

database::database()
    : database(":memory:")
{
}

database::database(const std::filesystem::path& file_name)
    : loggable("database")
{
    int rc = sqlite3_open_v2(file_name.c_str(),
                             &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXRESCODE,
                             nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error("Unable to open the database '"s + file_name.string() + "' " + error_msg(rc));
    lgr()->debug("Opened the database '{}'", file_name.string());
    char* err;
    rc = sqlite3_exec(db_,
                      schema,
                      nullptr,
                      nullptr,
                      &err);
    if (rc != SQLITE_OK)
    {
        // The file opened but isn't a usable database (most commonly: the
        // user picked a file that isn't a Nashville database at all).  This
        // used to abort, which is fine for the app's own controlled files but
        // wrong now that Open lets the user choose any file — throw so the
        // caller can report it and keep running.  close_v2 releases the handle
        // we opened above (deferred past any statements, though none exist
        // yet here).
        std::string msg = err ? err : "unknown error";
        sqlite3_free(err);
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("The file '"s + file_name.string() +
            "' is not a valid Nashville database: " + msg);
    }
    lgr()->debug("Successfully loaded the schema");
    check_version();
    try
    {
        struct stmt_def { statement key; const char* sql; };
        const stmt_def defs[] =
        {
            { statement::SELECT_CHORD,           SELECT_CHORD_SQL },
            { statement::SELECT_CHORD_BY_ID,     SELECT_CHORD_BY_ID_SQL },
            { statement::INSERT_CHORD,           INSERT_CHORD_SQL },
            { statement::SELECT_BAR,             SELECT_BAR_SQL },
            { statement::INSERT_BAR,             INSERT_BAR_SQL },
            { statement::SELECT_CHORDS_BY_BAR,   SELECT_CHORDS_BY_BAR_SQL },
            { statement::SELECT_TIME_SIGNATURE,  SELECT_TIME_SIGNATURE_SQL },
            { statement::INSERT_TIME_SIGNATURE,  INSERT_TIME_SIGNATURE_SQL },
            { statement::INSERT_BAR_CHORD,       INSERT_BAR_CHORD_SQL },
            { statement::SELECT_SONG_ID,         SELECT_SONG_ID_SQL },
            { statement::INSERT_SONG,            INSERT_SONG_SQL },
            { statement::UPDATE_SONG,            UPDATE_SONG_SQL },
            { statement::SELECT_SONG_BARS,       SELECT_SONG_BARS_SQL },
            { statement::INSERT_SONG_BAR,        INSERT_SONG_BAR_SQL },
            { statement::INSERT_PLAYLIST,        INSERT_PLAYLIST_SQL },
            { statement::SELECT_PLAYLIST_SONGS,  SELECT_PLAYLIST_SONGS_SQL },
            { statement::INSERT_PLAYLIST_SONG,   INSERT_PLAYLIST_SONG_SQL },
            { statement::SELECT_SONG_NAMES,      SELECT_SONG_NAMES_SQL },
            { statement::SELECT_SONG,            SELECT_SONG_SQL },
            { statement::SELECT_PLAYLIST_NAMES,  SELECT_PLAYLIST_NAMES_SQL },
            { statement::SELECT_PLAYLIST_ID,     SELECT_PLAYLIST_ID_SQL },
            { statement::SELECT_CHORD_NOT_IN_BAR, SELECT_CHORD_NOT_IN_BAR_SQL },
            { statement::REMOVE_CHORD,           REMOVE_CHORD_SQL },
            { statement::REMOVE_BAR,             REMOVE_BAR_SQL },
            { statement::REMOVE_SONG,            REMOVE_SONG_SQL },
            { statement::REMOVE_PLAYLIST,        REMOVE_PLAYLIST_SQL },
            { statement::INSERT_TEXT_BOX,            INSERT_TEXT_BOX_SQL },
            { statement::SELECT_SONG_TEXT_BOXES,     SELECT_SONG_TEXT_BOXES_SQL },
            { statement::INSERT_CONNECTOR,           INSERT_CONNECTOR_SQL },
            { statement::SELECT_SONG_CONNECTORS,     SELECT_SONG_CONNECTORS_SQL },
            { statement::DELETE_SONG_TEXT_BOXES,     DELETE_SONG_TEXT_BOXES_SQL },
            { statement::DELETE_SONG_CONNECTORS,     DELETE_SONG_CONNECTORS_SQL },
            { statement::RENAME_PLAYLIST,        RENAME_PLAYLIST_SQL }
        };

        for (const auto& d : defs)
            prepared_statements_.try_emplace(d.key, std::make_unique<prepared>(db_, d.sql));
    }
    catch (std::invalid_argument& e)
    {
        // A prepared-statement failed to compile.  As above, throw rather than
        // abort so a caller (e.g. open_file on a user-chosen file) can recover.
        // close_v2 defers the actual free until the partially-filled
        // prepared_statements_ map is unwound, finalizing any statements that
        // did compile.
        sqlite3_close_v2(db_);
        db_ = nullptr;
        throw std::runtime_error("Could not prepare the database '"s +
            file_name.string() + "': " + e.what());
    }
    lgr()->debug("Successfully created the prepared statements");
}

database::~database()
{
    prepared_statements_.clear();
    sqlite3_close(db_);
}

void database::check_version()
{
    std::string ver_str;
    prepared sel_ver(db_, "SELECT version FROM metadata;");
    int rc = sqlite3_step(sel_ver.ptr());
    if (rc == SQLITE_DONE)
    {
        prepared ins_ver(db_, "INSERT INTO metadata (version) VALUES (?1);");
        sqlite3_bind_int(ins_ver.ptr(), 1, CURRENT_VERSION);
        int rc2 = sqlite3_step(ins_ver.ptr());
        if (rc2 != SQLITE_DONE)
            throw std::runtime_error("Unable to insert the version into the database: "s + error_msg(rc2));
    }
    else if (rc == SQLITE_ROW)
    {
        auto found = sqlite3_column_int(sel_ver.ptr(), 0);
        if (found > MAX_SUPPORTED_VERSION)
        {
            throw std::runtime_error("The database version "s +
                    std::to_string(found) +
                    " is not supported in version " +
                    std::to_string(CURRENT_VERSION) +
                    " of Nashville");
        }
    }
    else
    {
        throw std::runtime_error("Unexpected error while checking the database version: "s + error_msg(rc));
    }
    lgr()->debug("Version check succeeded");
}

std::string database::error_msg(int rc) const
{
    return std::string(sqlite3_errstr(rc)) + "-" + sqlite3_errmsg(db_);
}

std::filesystem::path database::file_name() const
{
    std::filesystem::path p;
    auto fn = sqlite3_db_filename(db_, "main");
    if (fn != nullptr && std::strlen(fn) != 0)
        p = fn;
    return p;
}

std::uint64_t database::insert_bar(const model::bar& b)
{
    assert(prepared_statements_.count(statement::INSERT_BAR) == 1);
    auto& ins_b = prepared_statements_[statement::INSERT_BAR];
    ins_b->reset();
    auto raw = ins_b->ptr();
    if (b.time_sig())
        sqlite3_bind_int64(raw, 1, time_signature_id(*b.time_sig()));
    sqlite3_bind_int(raw, 2, b.is_eol());
    if (b.section())
        sqlite3_bind_text(raw, 3, b.section()->c_str(), b.section()->length(), SQLITE_STATIC);
    sqlite3_bind_int(raw, 4, static_cast<int>(b.repeat()));
    if (!b.voltas().empty())
    {
        std::ostringstream out;
        for (auto v : b.voltas())
            out << v << ',';
        auto vtext = out.str();
        vtext.pop_back();
        sqlite3_bind_text(raw, 5, vtext.c_str(), vtext.length(), SQLITE_STATIC);
    }
    if (b.number_of_beats())
        sqlite3_bind_int(raw, 6, *b.number_of_beats());
    if (!b.modulation().empty())
        sqlite3_bind_text(raw, 7, b.modulation().c_str(), b.modulation().length(), SQLITE_STATIC);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a bar: "s + error_msg(rc));
    lgr()->debug("Inserted bar: {}", b.to_user_input());
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

std::uint64_t database::insert_bar_chord(std::uint64_t bar_id, std::uint64_t chord_id, unsigned index)
{
    assert(prepared_statements_.count(statement::INSERT_BAR_CHORD) == 1);
    auto& ins_bc = prepared_statements_[statement::INSERT_BAR_CHORD];
    ins_bc->reset();
    auto raw = ins_bc->ptr();
    sqlite3_bind_int(raw, 1, chord_id);
    sqlite3_bind_int(raw, 2, bar_id);
    sqlite3_bind_int(raw, 3, index);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a bar chord: "s + error_msg(rc));
    lgr()->debug("Inserted relation bar({}) with chord({}) position {}", bar_id, chord_id, index);
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

std::uint64_t database::insert_chord(const model::chord& c)
{
    assert(prepared_statements_.count(statement::SELECT_CHORD) == 1);
    auto& sel_c = prepared_statements_[statement::SELECT_CHORD];
    sel_c->reset();
    auto raw = sel_c->ptr();
    sqlite3_bind_int(raw, 1, c.number());
    sqlite3_bind_int(raw, 2, static_cast<int>(c.mode()));
    if (c.step())
        sqlite3_bind_int(raw, 3, static_cast<int>(*c.step()));
    if (c.bass_note())
        sqlite3_bind_int(raw, 4, *c.bass_note());
    if (c.bass_note_step())
        sqlite3_bind_int(raw, 5, static_cast<int>(*c.bass_note_step()));
    sqlite3_bind_text(raw, 6, c.extensions().c_str(), c.extensions().length(), SQLITE_STATIC);
    sqlite3_bind_int(raw, 7, c.is_staccato());
    sqlite3_bind_int(raw, 8, c.is_diamond());
    if (c.duration())
        sqlite3_bind_int(raw, 9, static_cast<int>(*c.duration()));
    sqlite3_bind_int(raw, 10, c.is_tied());
    sqlite3_bind_int(raw, 11, c.is_pushed());
    auto rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW)
    {
        assert(sqlite3_column_count(raw) == 1);
        lgr()->debug("Found existing chord: {}", c.to_user_input());
        return sqlite3_column_int64(raw, 0);
    }
    assert(prepared_statements_.count(statement::INSERT_CHORD) == 1);
    auto& ins_c = prepared_statements_[statement::INSERT_CHORD];
    ins_c->reset();
    raw = ins_c->ptr();
    sqlite3_bind_int(raw, 1, c.number());
    sqlite3_bind_int(raw, 2, static_cast<int>(c.mode()));
    if (c.step())
        sqlite3_bind_int(raw, 3, static_cast<int>(*c.step()));
    if (c.bass_note())
        sqlite3_bind_int(raw, 4, *c.bass_note());
    if (c.bass_note_step())
        sqlite3_bind_int(raw, 5, static_cast<int>(*c.bass_note_step()));
    sqlite3_bind_text(raw, 6, c.extensions().c_str(), c.extensions().length(), SQLITE_STATIC);
    sqlite3_bind_int(raw, 7, c.is_staccato());
    sqlite3_bind_int(raw, 8, c.is_diamond());
    if (c.duration())
        sqlite3_bind_int(raw, 9, static_cast<int>(*c.duration()));
    sqlite3_bind_int(raw, 10, c.is_tied());
    sqlite3_bind_int(raw, 11, c.is_pushed());
    rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a chord: "s + error_msg(rc));
    assert(sqlite3_column_count(raw) == 1);
    lgr()->debug("Inserted new chord: {}", c.to_user_input());
    return sqlite3_column_int64(raw, 0);
}

playlist_id database::insert_playlist(const model::playlist& pl)
{
    assert(prepared_statements_.count(statement::INSERT_PLAYLIST) == 1);
    assert(prepared_statements_.count(statement::SELECT_SONG_ID) == 1);
    assert(prepared_statements_.count(statement::INSERT_PLAYLIST_SONG) == 1);
    auto& ins_pl = prepared_statements_[statement::INSERT_PLAYLIST];
    auto& sel_sid = prepared_statements_[statement::SELECT_SONG_ID];
    auto& ins_ps = prepared_statements_[statement::INSERT_PLAYLIST_SONG];
    auto raw = ins_pl->ptr();
    int rc;
    transaction tx(*this);
    ins_pl->reset();
    sqlite3_bind_text(raw, 1, pl.name().c_str(), pl.name().length(), SQLITE_STATIC);
    rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert playlist '"s + pl.name() + "':" + error_msg(rc));
    auto pl_id = sqlite3_column_int64(raw, 0);
    for (int i = 0; i < pl.songs().size(); i++)
    {
        sel_sid->reset();
        const auto& n = pl.songs()[i];
        sqlite3_bind_text(sel_sid->ptr(), 1, n.c_str(), n.length(), SQLITE_STATIC);
        auto rc2 = sqlite3_step(sel_sid->ptr());
        if (rc2 == SQLITE_ROW)
        {
            ins_ps->reset();
            sqlite3_bind_int64(ins_ps->ptr(), 1, sqlite3_column_int64(sel_sid->ptr(), 0));
            sqlite3_bind_int64(ins_ps->ptr(), 2, pl_id);
            sqlite3_bind_int(ins_ps->ptr(), 3, i);
            rc2 = sqlite3_step(ins_ps->ptr());
            if (rc2 != SQLITE_DONE)
            {
                throw std::runtime_error("Could not insert a playlist song reference for playlist '"s +
                        pl.name() + "', song '" + n + "': " + error_msg(rc2));
            }
        }
        else
        {
            throw std::runtime_error("Problem looking up a song named '"s + n + "':" + error_msg(rc2));
        }
    }
    tx.commit();
    lgr()->debug("Inserted new playlist '{}' (id {})", pl.name(), pl_id);
    return playlist_id{ pl_id };
}

// Binds the 14 song-table columns as parameters ?1..?14.  The caller must
// reset() the statement first (reset clears bindings, so any skipped optional
// column lands as NULL — which is exactly what clears an emptied authors list
// or release date on update).  INSERT_SONG and UPDATE_SONG deliberately share
// this parameter layout so a single binder serves both.
void database::bind_song_columns(sqlite3_stmt* raw, const model::song& s)
{
    sqlite3_bind_text(raw, 1, s.name().c_str(), s.name().length(), SQLITE_STATIC);
    sqlite3_bind_text(raw, 2, s.key().c_str(), s.key().length(), SQLITE_STATIC);
    sqlite3_bind_int64(raw, 3, time_signature_id(s.time_sig()));
    sqlite3_bind_int(raw, 4, s.bars_per_line());
    sqlite3_bind_int(raw, 5, std::get<0>(s.tempo()));
    sqlite3_bind_int(raw, 6, static_cast<int>(std::get<1>(s.tempo())));
    const auto& meta = s.meta();
    auto ctime = format_iso8601(meta.creation_time);
    sqlite3_bind_text(raw, 7, ctime.c_str(), ctime.length(), SQLITE_TRANSIENT);
    auto mtime = format_iso8601(meta.modification_time);
    sqlite3_bind_text(raw, 8, mtime.c_str(), mtime.length(), SQLITE_TRANSIENT);
    if (!meta.authors.empty())
    {
        std::ostringstream auth;
        for (int i = 0; i < meta.authors.size(); i++)
        {
            auth << meta.authors[i];
            if (i < meta.authors.size() - 1)
                auth << ',';
        }
        auto auths = auth.str();
        sqlite3_bind_text(raw, 9, auths.c_str(), auths.length(), SQLITE_TRANSIENT);
    }
    if (!meta.original_performer.empty())
        sqlite3_bind_text(raw, 10, meta.original_performer.c_str(), meta.original_performer.length(), SQLITE_STATIC);
    if (!meta.original_album.empty())
        sqlite3_bind_text(raw, 11, meta.original_album.c_str(), meta.original_album.length(), SQLITE_STATIC);
    if (!meta.notes.empty())
        sqlite3_bind_text(raw, 12, meta.notes.c_str(), meta.notes.length(), SQLITE_STATIC);
    if (meta.original_album_release_date)
    {
        auto rdate = format_iso8601(*meta.original_album_release_date);
        sqlite3_bind_text(raw, 13, rdate.c_str(), rdate.length(), SQLITE_TRANSIENT);
    }
    if (s.margin_width())
        sqlite3_bind_int(raw, 14, *s.margin_width());
}

// Rewrites the bars + annotations for an existing song row (identified by
// song_id).  Clears the prior set first so the persisted state matches the
// in-memory state exactly with no stale rows.  Runs inside the caller's
// transaction; the caller owns commit.
void database::write_song_body(std::int64_t song_id, const model::song& s)
{
    // Clear any bars from a previous save before writing the current set.
    // For a brand-new song this is a no-op.
    remove_song_bars(song_id);
    for (unsigned i = 0; i < s.bars().size(); i++)
    {
        auto& cur_bar = s.bars()[i];
        auto bar_id = insert_bar(cur_bar);
        for (unsigned j = 0; j < cur_bar.chords().size(); j++)
        {
            auto& cur_chord = cur_bar.chords()[j];
            auto chord_id = insert_chord(cur_chord);
            insert_bar_chord(bar_id, chord_id, j);
        }
        insert_song_bar(song_id, bar_id, i);
    }

    // --- Annotations ---
    // Delete any existing annotation rows for this song first.  For a
    // brand-new song this is a no-op.  For an update it purges the
    // stale set so the current in-memory state is written cleanly.
    // Connectors must be deleted before text boxes to avoid triggering
    // the ON DELETE SET NULL FK cascade on start_text_box_id /
    // end_text_box_id unnecessarily.
    {
        auto& del_c = prepared_statements_[statement::DELETE_SONG_CONNECTORS];
        del_c->reset();
        sqlite3_bind_int64(del_c->ptr(), 1, song_id);
        auto rc_dc = sqlite3_step(del_c->ptr());
        if (rc_dc != SQLITE_DONE)
            throw std::runtime_error(
                "Could not delete connectors for song '"s + s.name()
                + "': " + error_msg(rc_dc));
    }
    {
        auto& del_tb = prepared_statements_[statement::DELETE_SONG_TEXT_BOXES];
        del_tb->reset();
        sqlite3_bind_int64(del_tb->ptr(), 1, song_id);
        auto rc_dtb = sqlite3_step(del_tb->ptr());
        if (rc_dtb != SQLITE_DONE)
            throw std::runtime_error(
                "Could not delete text_boxes for song '"s + s.name()
                + "': " + error_msg(rc_dtb));
    }
    // Same transaction, so a half-written annotation set won't leave a
    // partially-saved song.  Text boxes first then connectors —
    // connectors may have endpoints glued to text boxes, so we need
    // the text-box rows (and their RETURNING-assigned database ids)
    // before we can bind anchor references on the connector rows.
    // FK to song(id) is in place by this point (song_id is an existing
    // row), so no FK violations are possible.
    //
    // text_box_id_map maps in-memory annotation ids to the database
    // rowids SQLite assigns on RETURNING.  These will usually differ
    // (the in-memory ids come from annotations::next_id_, which is
    // session-local; the database picks its own).  We only need the
    // map within this transaction, and connectors that reference
    // text-box ids read from this map.
    std::map<std::uint64_t, std::uint64_t> text_box_id_map;
    {
        auto& ins_tb = prepared_statements_[statement::INSERT_TEXT_BOX];
        for (const auto& tb : s.annotes().text_boxes())
        {
            ins_tb->reset();
            auto tb_raw = ins_tb->ptr();
            sqlite3_bind_int64 (tb_raw, 1, song_id);
            sqlite3_bind_double(tb_raw, 2, tb.rect.x());
            sqlite3_bind_double(tb_raw, 3, tb.rect.y());
            sqlite3_bind_double(tb_raw, 4, tb.rect.width());
            sqlite3_bind_double(tb_raw, 5, tb.rect.height());
            const QByteArray txt = tb.text.toUtf8();
            // SQLITE_TRANSIENT because the QByteArray dies at end of
            // scope; SQLite copies the bytes for us.
            sqlite3_bind_text  (tb_raw, 6, txt.constData(), txt.size(),
                                SQLITE_TRANSIENT);
            auto rc_tb = sqlite3_step(tb_raw);
            if (rc_tb != SQLITE_ROW)
                throw std::runtime_error(
                    "Could not insert text_box for song '"s + s.name()
                    + "': " + error_msg(rc_tb));
            const std::uint64_t db_id = sqlite3_column_int64(tb_raw, 0);
            text_box_id_map[tb.id] = db_id;
        }
    }
    {
        auto& ins_c = prepared_statements_[statement::INSERT_CONNECTOR];
        for (const auto& c : s.annotes().connectors())
        {
            ins_c->reset();
            auto c_raw = ins_c->ptr();
            sqlite3_bind_int64 (c_raw, 1, song_id);

            // For each endpoint: write the resolved free-position
            // coordinates (so a defensive reader has a fallback) plus
            // the anchor reference if applicable.  Resolved positions
            // for glued endpoints come from the model's
            // resolver-of-record — which is the same lambda the view
            // installed on the model at startup, executed via
            // anchor_resolver.  But the database layer doesn't have
            // that lambda in scope; the natural place to compute the
            // fallback xN/yN for glued endpoints is the model itself.
            // We do it here inline because the geometry is trivial
            // (the 8 anchor positions on a known QRectF) — duplicating
            // it across two layers is cheaper than plumbing a callback.
            auto fallback_xy =
                [&](const model::connector_endpoint& ep) -> QPointF {
                    if (ep.k == model::connector_endpoint::kind::free)
                        return ep.free_pos;
                    const auto* tb = s.annotes().find_text_box(ep.text_box_id);
                    if (!tb || ep.anchor_index >= 8)
                        return QPointF(0, 0);
                    const QRectF& r = tb->rect;
                    const qreal cx = r.center().x(), cy = r.center().y();
                    switch (ep.anchor_index)
                    {
                    case 0: return { r.left(),  r.top()    };
                    case 1: return { cx,        r.top()    };
                    case 2: return { r.right(), r.top()    };
                    case 3: return { r.right(), cy         };
                    case 4: return { r.right(), r.bottom() };
                    case 5: return { cx,        r.bottom() };
                    case 6: return { r.left(),  r.bottom() };
                    case 7: return { r.left(),  cy         };
                    }
                    return QPointF(0, 0);
                };
            const QPointF s_xy = fallback_xy(c.start);
            const QPointF e_xy = fallback_xy(c.end);
            sqlite3_bind_double(c_raw, 2, s_xy.x());
            sqlite3_bind_double(c_raw, 3, s_xy.y());
            sqlite3_bind_double(c_raw, 4, e_xy.x());
            sqlite3_bind_double(c_raw, 5, e_xy.y());
            sqlite3_bind_int   (c_raw, 6, c.arrow_at_start ? 1 : 0);
            sqlite3_bind_int   (c_raw, 7, c.arrow_at_end   ? 1 : 0);

            // Anchor references: bind NULL for free endpoints, the
            // mapped DB id and anchor index for glued ones.  If a
            // glued endpoint references a text box we didn't insert
            // (defensive — every glued endpoint's box should be in
            // the same annotations set), we treat it as free.  Both
            // text_box_id and anchor_index columns go together — we
            // either bind both or NULL both.
            auto bind_anchor_ref =
                [&](int id_col, int idx_col,
                    const model::connector_endpoint& ep) {
                    if (ep.k == model::connector_endpoint::kind::text_box_anchor)
                    {
                        auto it = text_box_id_map.find(ep.text_box_id);
                        if (it != text_box_id_map.end())
                        {
                            sqlite3_bind_int64(c_raw, id_col,
                                               static_cast<sqlite3_int64>(it->second));
                            sqlite3_bind_int  (c_raw, idx_col,
                                               static_cast<int>(ep.anchor_index));
                            return;
                        }
                    }
                    sqlite3_bind_null(c_raw, id_col);
                    sqlite3_bind_null(c_raw, idx_col);
                };
            bind_anchor_ref(8, 9, c.start);
            bind_anchor_ref(10, 11, c.end);

            auto rc_c = sqlite3_step(c_raw);
            if (rc_c != SQLITE_ROW)
                throw std::runtime_error(
                    "Could not insert connector for song '"s + s.name()
                    + "': " + error_msg(rc_c));
        }
    }
}

// Create a brand-new song row and return its identity.  The caller (a tab on
// its first save, or the New-song flow) keeps the returned id and uses
// update_song for every subsequent save, so identity never rides on the
// (mutable) name.
song_id database::insert_song(const model::song& s)
{
    assert(prepared_statements_.count(statement::INSERT_SONG) == 1);
    auto& ins_s = prepared_statements_[statement::INSERT_SONG];
    auto raw = ins_s->ptr();
    transaction tx(*this);
    ins_s->reset();
    bind_song_columns(raw, s);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Unable to insert song '"s + s.name() + "': " + error_msg(rc));
    assert(sqlite3_column_count(raw) == 1);
    const std::int64_t new_id = sqlite3_column_int64(raw, 0);
    write_song_body(new_id, s);
    tx.commit();
    lgr()->debug("Inserted song '{}' (id {})", s.name(), new_id);
    return song_id{ new_id };
}

// Overwrite an existing song row, located by id.  Because the row is found by
// id, the name is just another column in the write — a rename is an ordinary
// update, with no old-name lookup and no chance of orphaning the prior row.
void database::update_song(song_id id, const model::song& s)
{
    assert(prepared_statements_.count(statement::UPDATE_SONG) == 1);
    auto& upd_s = prepared_statements_[statement::UPDATE_SONG];
    auto raw = upd_s->ptr();
    const std::int64_t rid = static_cast<std::int64_t>(id);
    transaction tx(*this);
    upd_s->reset();
    bind_song_columns(raw, s);                 // ?1..?14
    sqlite3_bind_int64(raw, 15, rid);          // WHERE id = ?15
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Unable to update song '"s + s.name() + "': " + error_msg(rc));
    if (sqlite3_changes(db_) == 0)
        throw std::runtime_error("Cannot update song '"s + s.name()
            + "': no song with id " + std::to_string(rid));
    write_song_body(rid, s);
    tx.commit();
    lgr()->debug("Updated song '{}' (id {})", s.name(), rid);
}

std::uint64_t database::insert_song_bar(std::uint64_t song_id, std::uint64_t bar_id, unsigned index)
{
    assert(prepared_statements_.count(statement::INSERT_SONG_BAR) == 1);
    auto& ins_sb = prepared_statements_[statement::INSERT_SONG_BAR];
    ins_sb->reset();
    auto raw = ins_sb->ptr();
    sqlite3_bind_int(raw, 1, bar_id);
    sqlite3_bind_int(raw, 2, song_id);
    sqlite3_bind_int(raw, 3, index);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a song bar: "s + error_msg(rc));
    lgr()->debug("Inserted relation song({}) with bar({}) position {}", song_id, bar_id, index);
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

void database::maybe_remove_chord(std::uint64_t bar_id, std::uint64_t chord_id)
{
    assert(prepared_statements_.count(statement::SELECT_CHORD_NOT_IN_BAR) == 1);
    auto& sel_c = prepared_statements_[statement::SELECT_CHORD_NOT_IN_BAR];
    sel_c->reset();
    sqlite3_bind_int64(sel_c->ptr(), 1, chord_id);
    sqlite3_bind_int64(sel_c->ptr(), 2, bar_id);
    auto rc = sqlite3_step(sel_c->ptr());
    if (rc == SQLITE_ROW)
        return;
    assert(prepared_statements_.count(statement::REMOVE_CHORD) == 1);
    auto& rem_c = prepared_statements_[statement::REMOVE_CHORD];
    rem_c->reset();
    sqlite3_bind_int64(rem_c->ptr(), 1, chord_id);
    rc = sqlite3_step(rem_c->ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not remove a chord: "s + error_msg(rc));
    lgr()->debug("Removed chord with id {}", chord_id);
}

void database::move_to_file(const std::filesystem::path& file_name)
{
    auto abs = std::filesystem::absolute(file_name).lexically_normal();

    // Write to a sibling temp file first, then atomically rename it over the
    // target.  This keeps the operation all-or-nothing with respect to disk:
    // a failure (full disk, read-only volume, a backup error) can leave a
    // discarded temp file but can never damage an existing file at `abs`, and
    // it never leaves *this half-transitioned — every throw below happens
    // before the in-memory connection is swapped out, and the swap itself is
    // noexcept.
    auto tmp = abs;
    tmp += ".saving-tmp";

    // Clear any leftover temp from a previously interrupted save so we start
    // from a clean file (best-effort; a failure to remove surfaces later).
    std::error_code ec;
    std::filesystem::remove(tmp, ec);

    // Back the in-memory database up into the temp file.  The backup reads
    // from db_ (the source) and writes only to other.db_ (the temp), so the
    // in-memory data is never modified regardless of where this fails.
    bool   init_failed = false;
    std::string init_err;
    int    step_rc = SQLITE_OK;
    int    finish_rc = SQLITE_OK;
    {
        database other(tmp);   // creates/opens the temp file + its statements
        auto back = sqlite3_backup_init(other.db_, "main", db_, "main");
        if (back == nullptr)
        {
            init_failed = true;
            init_err = other.error_msg(sqlite3_errcode(other.db_));
        }
        else
        {
            // With nPage = -1, step copies the whole database and returns
            // SQLITE_DONE on success; finish then returns SQLITE_OK (or the
            // error from a failed step).  So a clean save is DONE *and* OK.
            step_rc = sqlite3_backup_step(back, -1);
            finish_rc = sqlite3_backup_finish(back);
        }
    }   // other destroyed here: the temp file is flushed and closed before we
        // touch it on disk below (matters on platforms that lock open files).

    if (init_failed || step_rc != SQLITE_DONE || finish_rc != SQLITE_OK)
    {
        std::filesystem::remove(tmp, ec);   // best-effort cleanup
        const int err_rc = (step_rc != SQLITE_DONE) ? step_rc : finish_rc;
        throw std::runtime_error("Could not write the database to file '"s +
                                 abs.string() + "': " +
                                 (init_failed ? init_err : error_msg(err_rc)));
    }

    // Atomically replace any existing target with the freshly written temp.
    // rename() is atomic within a single filesystem, and the temp is a sibling
    // of the target, so this holds.  Until this line, `abs` is untouched.
    std::filesystem::rename(tmp, abs, ec);
    if (ec)
    {
        std::error_code rm_ec;
        std::filesystem::remove(tmp, rm_ec);   // best-effort cleanup
        throw std::runtime_error("Could not finalize saving the database to '"s +
                                 abs.string() + "': " + ec.message());
    }

    // Re-open the now-final file as the live connection and commit by swapping
    // it into *this.  If this open fails, the data is already safely on disk
    // (the rename succeeded) — we throw and stay on the in-memory database, so
    // nothing is lost (the user can reopen the file) and *this is never left
    // half-transitioned.
    database other(abs);
    std::swap(db_, other.db_);
    std::swap(prepared_statements_, other.prepared_statements_);

    lgr()->info("Saved the in-memory database to the file '{}'", abs.string());
}

void database::open_file(const std::filesystem::path& file_name)
{
    auto abs = std::filesystem::absolute(file_name).lexically_normal();

    // Open the target as its own database (which prepares its statements and
    // ensures the schema), then commit by swapping it into *this with the same
    // noexcept swap move_to_file uses.  This discards whatever *this currently
    // held — the caller is responsible for having saved or deliberately
    // discarded it first.
    //
    // Failure safety mirrors move_to_file: if the file can't be opened or
    // isn't a valid database, the `other` constructor throws here, before the
    // swap, so *this is left exactly as it was — same connection, same
    // statements, nothing lost.
    database other(abs);
    std::swap(db_, other.db_);
    std::swap(prepared_statements_, other.prepared_statements_);

    lgr()->info("Opened the database file '{}'", abs.string());
}

void database::remove_playlist(const std::string& pl)
{
    assert(prepared_statements_.count(statement::REMOVE_PLAYLIST) == 1);
    auto& rm_pl = prepared_statements_[statement::REMOVE_PLAYLIST];
    rm_pl->reset();
    sqlite3_bind_text(rm_pl->ptr(), 1, pl.c_str(), pl.length(), SQLITE_STATIC);
    transaction tx(*this);
    auto rc = sqlite3_step(rm_pl->ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not remove playlist '"s + pl + "': " + error_msg(rc));
    tx.commit();
    lgr()->debug("Removed playlist '{}'", pl);
}

void database::remove_song_bars(std::uint64_t song_id)
{
    assert(prepared_statements_.count(statement::SELECT_SONG_BARS) == 1);
    assert(prepared_statements_.count(statement::SELECT_CHORDS_BY_BAR) == 1);
    assert(prepared_statements_.count(statement::REMOVE_BAR) == 1);

    auto& sel_bids = prepared_statements_[statement::SELECT_SONG_BARS];
    auto& sel_cs   = prepared_statements_[statement::SELECT_CHORDS_BY_BAR];
    auto& rem_b    = prepared_statements_[statement::REMOVE_BAR];

    // Collect all bar IDs for this song before deleting anything,
    // because deleting bar rows cascades song_bars rows away.
    std::vector<std::int64_t> bar_ids;
    sel_bids->reset();
    sqlite3_bind_int64(sel_bids->ptr(), 1, song_id);
    auto rc = sqlite3_step(sel_bids->ptr());
    while (rc == SQLITE_ROW)
    {
        bar_ids.push_back(sqlite3_column_int64(sel_bids->ptr(), 0));
        rc = sqlite3_step(sel_bids->ptr());
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not read song bars: "s + error_msg(rc));

    // For each bar: clean up its chords, then delete the bar.
    // bar_chords rows cascade away when the bar is deleted.
    for (auto bar_id : bar_ids)
    {
        std::vector<std::int64_t> chord_ids;
        sel_cs->reset();
        sqlite3_bind_int64(sel_cs->ptr(), 1, bar_id);
        auto rc2 = sqlite3_step(sel_cs->ptr());
        while (rc2 == SQLITE_ROW)
        {
            chord_ids.push_back(sqlite3_column_int64(sel_cs->ptr(), 0));
            rc2 = sqlite3_step(sel_cs->ptr());
        }
        if (rc2 != SQLITE_DONE)
            throw std::runtime_error("Could not read bar chords: "s + error_msg(rc2));

        for (auto chord_id : chord_ids)
            maybe_remove_chord(bar_id, chord_id);

        rem_b->reset();
        sqlite3_bind_int64(rem_b->ptr(), 1, bar_id);
        auto rc3 = sqlite3_step(rem_b->ptr());
        if (rc3 != SQLITE_DONE)
            throw std::runtime_error("Could not remove bar: "s + error_msg(rc3));
        lgr()->debug("Removed bar with id {}", bar_id);
    }
}

void database::remove_song(const std::string& s)
{
    assert(prepared_statements_.count(statement::SELECT_SONG_ID) == 1);
    assert(prepared_statements_.count(statement::REMOVE_SONG) == 1);

    auto& sel_s = prepared_statements_[statement::SELECT_SONG_ID];
    auto& rem_s = prepared_statements_[statement::REMOVE_SONG];

    transaction tx(*this);

    // Look up the song's ID. If it doesn't exist, there's nothing to do.
    sel_s->reset();
    sqlite3_bind_text(sel_s->ptr(), 1, s.c_str(), s.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(sel_s->ptr());
    if (rc == SQLITE_DONE)
        return;  // no such song
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not look up song '"s + s + "': " + error_msg(rc));
    auto song_id = sqlite3_column_int64(sel_s->ptr(), 0);

    remove_song_bars(song_id);

    // Remove the song row itself. Any remaining song_bars rows cascade away,
    // as do text_box and connector rows.
    rem_s->reset();
    sqlite3_bind_int64(rem_s->ptr(), 1, song_id);
    auto rc5 = sqlite3_step(rem_s->ptr());
    if (rc5 != SQLITE_DONE)
        throw std::runtime_error("Could not remove song '"s + s + "': " + error_msg(rc5));

    tx.commit();

    lgr()->debug("Removed the song '{}'", s);
}

std::optional<playlist_id> database::playlist_id_of(const std::string& name)
{
    assert(prepared_statements_.count(statement::SELECT_PLAYLIST_ID) == 1);
    auto& sel_pl = prepared_statements_[statement::SELECT_PLAYLIST_ID];
    sel_pl->reset();
    sqlite3_bind_text(sel_pl->ptr(), 1, name.c_str(), name.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(sel_pl->ptr());
    if (rc == SQLITE_ROW)
        return playlist_id{ sqlite3_column_int64(sel_pl->ptr(), 0) };
    if (rc == SQLITE_DONE)
        return std::nullopt;
    throw std::runtime_error("Could not look up playlist '"s + name + "': " + error_msg(rc));
}

void database::rename_playlist(playlist_id id, const std::string& new_name)
{
    assert(prepared_statements_.count(statement::RENAME_PLAYLIST) == 1);
    auto& ren = prepared_statements_[statement::RENAME_PLAYLIST];
    const std::int64_t rid = static_cast<std::int64_t>(id);
    transaction tx(*this);
    ren->reset();
    sqlite3_bind_text(ren->ptr(), 1, new_name.c_str(), new_name.length(), SQLITE_STATIC);
    sqlite3_bind_int64(ren->ptr(), 2, rid);
    auto rc = sqlite3_step(ren->ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Error renaming playlist to '"s + new_name + "': "s + error_msg(rc));
    if (sqlite3_changes(db_) == 0)
        throw std::runtime_error("Cannot rename playlist to '"s + new_name
            + "': no playlist with id " + std::to_string(rid));
    tx.commit();
    lgr()->info("Renamed playlist id {} to '{}'", rid, new_name);
}


std::vector<model::bar> database::select_bars(std::uint64_t song_id)
{
    std::vector<model::bar> bars;
    assert(prepared_statements_.count(statement::SELECT_SONG_BARS) == 1);
    assert(prepared_statements_.count(statement::SELECT_BAR) == 1);
    auto& sel_bs = prepared_statements_[statement::SELECT_SONG_BARS];
    sel_bs->reset();
    auto raw = sel_bs->ptr();
    auto& sel_b = prepared_statements_[statement::SELECT_BAR];
    sqlite3_bind_int64(raw, 1, song_id);
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        sel_b->reset();
        auto bar_id = sqlite3_column_int64(raw, 0);
        sqlite3_bind_int64(sel_b->ptr(), 1, bar_id);
        auto rc2 = sqlite3_step(sel_b->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error("Could not look up bar by ID: "s + error_msg(rc2));
        model::bar bar;
        bar.is_eol(sqlite3_column_int(sel_b->ptr(), 0));
        if (sqlite3_column_type(sel_b->ptr(), 1) == SQLITE_TEXT)
            bar.section(reinterpret_cast<const char*>(sqlite3_column_text(sel_b->ptr(), 1)));
        else
            assert(sqlite3_column_type(sel_b->ptr(), 1) == SQLITE_NULL);
        if (sqlite3_column_type(sel_b->ptr(), 2) == SQLITE_INTEGER &&
            sqlite3_column_type(sel_b->ptr(), 3) == SQLITE_INTEGER)
        {
            model::time_signature ts;
            ts.kind(static_cast<model::time_signature::beat_type>(sqlite3_column_int(sel_b->ptr(), 2)))
              .count(sqlite3_column_int(sel_b->ptr(), 3));
            bar.time_sig(ts);
        }
        else
        {
            assert(sqlite3_column_type(sel_b->ptr(), 2) == SQLITE_NULL &&
                   sqlite3_column_type(sel_b->ptr(), 3) == SQLITE_NULL);
        }
        bar.repeat(static_cast<model::bar::repeat_status>(sqlite3_column_int(sel_b->ptr(), 4)));
        if (sqlite3_column_type(sel_b->ptr(), 5) == SQLITE_TEXT)
        {
            std::istringstream in(reinterpret_cast<const char*>(sqlite3_column_text(sel_b->ptr(), 5)));
            std::string idx;
            while (std::getline(in, idx, ','))
                bar.add_volta(std::stoi(idx));
        }
        else
        {
            assert(sqlite3_column_type(sel_b->ptr(), 5) == SQLITE_NULL);
        }
        if (sqlite3_column_type(sel_b->ptr(), 6) == SQLITE_INTEGER)
            bar.number_of_beats(sqlite3_column_int(sel_b->ptr(), 6));
        else
            assert(sqlite3_column_type(sel_b->ptr(), 6) == SQLITE_NULL);
        if (sqlite3_column_type(sel_b->ptr(), 7) == SQLITE_TEXT)
            bar.modulation(reinterpret_cast<const char*>(sqlite3_column_text(sel_b->ptr(), 7)));
        else
            assert(sqlite3_column_type(sel_b->ptr(), 7) == SQLITE_NULL);

        bar.chords(select_chords(bar_id));
        bars.push_back(bar);
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve bars: "s + error_msg(rc));
    return bars;
}

std::vector<model::chord> database::select_chords(std::uint64_t bar_id)
{
    std::vector<model::chord> chords;
    assert(prepared_statements_.count(statement::SELECT_CHORDS_BY_BAR) == 1);
    assert(prepared_statements_.count(statement::SELECT_CHORD_BY_ID) == 1);
    auto& sel_cs = prepared_statements_[statement::SELECT_CHORDS_BY_BAR];
    sel_cs->reset();
    auto raw = sel_cs->ptr();
    sqlite3_bind_int64(raw, 1, bar_id);
    auto& sel_c = prepared_statements_[statement::SELECT_CHORD_BY_ID];
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        model::chord ch;
        sel_c->reset();
        sqlite3_bind_int64(sel_c->ptr(), 1, sqlite3_column_int64(raw, 0));
        auto rc2 = sqlite3_step(sel_c->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error("Could not look up chord by ID: "s + error_msg(rc2));
        ch.number(sqlite3_column_int(sel_c->ptr(), 1))
          .mode(static_cast<model::chord::type>(sqlite3_column_int(sel_c->ptr(), 2)));
        if (sqlite3_column_type(sel_c->ptr(), 3) == SQLITE_INTEGER)
            ch.step(static_cast<model::chord::flat_sharp>(sqlite3_column_int(sel_c->ptr(), 3)));
        else
            assert(sqlite3_column_type(sel_c->ptr(), 3) == SQLITE_NULL);
        if (sqlite3_column_type(sel_c->ptr(), 4) == SQLITE_INTEGER)
            ch.bass_note(sqlite3_column_int(sel_c->ptr(), 4));
        else
            assert(sqlite3_column_type(sel_c->ptr(), 4) == SQLITE_NULL);
        if (sqlite3_column_type(sel_c->ptr(), 5) == SQLITE_INTEGER)
            ch.bass_note_step(static_cast<model::chord::flat_sharp>(sqlite3_column_int(sel_c->ptr(), 5)));
        else
            assert(sqlite3_column_type(sel_c->ptr(), 5) == SQLITE_NULL);
        if (sqlite3_column_type(sel_c->ptr(), 6) == SQLITE_TEXT)
            ch.extensions(reinterpret_cast<const char*>(sqlite3_column_text(sel_c->ptr(), 6)));
        else
            assert(sqlite3_column_type(sel_c->ptr(), 6) == SQLITE_NULL);
        ch.is_staccato(sqlite3_column_int(sel_c->ptr(), 7))
          .is_diamond(sqlite3_column_int(sel_c->ptr(), 8));
        if (sqlite3_column_type(sel_c->ptr(), 9) == SQLITE_INTEGER)
            ch.duration(static_cast<model::chord::time>(sqlite3_column_int(sel_c->ptr(), 9)));
        else
            assert(sqlite3_column_type(sel_c->ptr(), 9) == SQLITE_NULL);
        ch.is_tied(sqlite3_column_int(sel_c->ptr(), 10))
          .is_pushed(sqlite3_column_int(sel_c->ptr(), 11));
        chords.push_back(ch);
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve chords: "s + error_msg(rc));
    return chords;
}

model::playlist database::select_playlist(const std::string& name)
{
    model::playlist p(name);
    assert(prepared_statements_.count(statement::SELECT_PLAYLIST_ID) == 1);
    auto& sel_p = prepared_statements_[statement::SELECT_PLAYLIST_ID];
    transaction tx(*this);
    sel_p->reset();
    sqlite3_bind_text(sel_p->ptr(), 1, name.c_str(), name.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(sel_p->ptr());
    if (rc == SQLITE_ROW)
    {
        assert(prepared_statements_.count(statement::SELECT_PLAYLIST_SONGS) == 1);
        auto& sel_ps = prepared_statements_[statement::SELECT_PLAYLIST_SONGS];
        sel_ps->reset();
        auto raw = sel_ps->ptr();
        sqlite3_bind_int64(raw, 1, sqlite3_column_int64(sel_p->ptr(), 0));
        auto rc2 = sqlite3_step(raw);
        while (rc2 == SQLITE_ROW)
        {
            p.add_song(reinterpret_cast<const char*>(sqlite3_column_text(raw, 0)));
            rc2 = sqlite3_step(raw);
        }
        if (rc2 != SQLITE_DONE)
            throw std::runtime_error("Error retrieving playlist songs: "s + error_msg(rc));
    }
    else if (rc != SQLITE_DONE)
    {
        throw std::runtime_error("Error retrieving playlist: "s + error_msg(rc));
    }
    tx.commit();
    return p;
}

std::vector<std::string> database::select_playlist_names()
{
    std::vector<std::string> names;
    assert(prepared_statements_.count(statement::SELECT_PLAYLIST_NAMES) == 1);
    auto& sel_p = prepared_statements_[statement::SELECT_PLAYLIST_NAMES];
    sel_p->reset();
    auto raw = sel_p->ptr();
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        names.push_back(reinterpret_cast<const char*>(sqlite3_column_text(raw, 0)));
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve playlist names: "s + error_msg(rc));
    return names;
}

std::vector<std::string> database::select_song_names()
{
    std::vector<std::string> names;
    assert(prepared_statements_.count(statement::SELECT_SONG_NAMES) == 1);
    auto& sel_s = prepared_statements_[statement::SELECT_SONG_NAMES];
    sel_s->reset();
    auto raw = sel_s->ptr();
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        names.push_back(reinterpret_cast<const char*>(sqlite3_column_text(raw, 0)));
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve song names: "s + error_msg(rc));
    return names;
}

stored_song database::select_song(const std::string& name)
{
    model::song found(name);
    assert(prepared_statements_.count(statement::SELECT_SONG) == 1);
    auto& sel_s = prepared_statements_[statement::SELECT_SONG];
    sel_s->reset();
    auto raw = sel_s->ptr();
    transaction tx(*this);
    sqlite3_bind_text(raw, 1, name.c_str(), name.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(raw);
    std::int64_t row_id = 0;
    if (rc == SQLITE_ROW)
    {
        row_id = sqlite3_column_int64(raw, 0);
        found.key(reinterpret_cast<const char*>(sqlite3_column_text(raw, 1)));
        found.bars_per_line(sqlite3_column_int(raw, 2));
        found.tempo(std::make_tuple(sqlite3_column_int(raw, 3), static_cast<model::chord::time>(sqlite3_column_int(raw, 4))));
        found.bars(select_bars(row_id));
        model::time_signature ts;
        ts.kind(static_cast<model::time_signature::beat_type>(sqlite3_column_int(raw, 5)))
          .count(sqlite3_column_int(raw, 6));
        found.time_sig(ts);
        auto& meta = found.meta();
        meta.creation_time = std::chrono::time_point_cast<std::chrono::milliseconds>
            (parse_iso8601(reinterpret_cast<const char*>(sqlite3_column_text(raw, 7))));
        meta.modification_time = std::chrono::time_point_cast<std::chrono::milliseconds>
            (parse_iso8601(reinterpret_cast<const char*>(sqlite3_column_text(raw, 8))));
        if (sqlite3_column_type(raw, 9) == SQLITE_TEXT)
        {
            std::string cur;
            std::istringstream in(reinterpret_cast<const char*>(sqlite3_column_text(raw, 9)));
            while (std::getline(in, cur, ','))
                meta.authors.push_back(cur);
        }
        else
        {
            assert(sqlite3_column_type(raw, 9) == SQLITE_NULL);
        }
        if (sqlite3_column_type(raw, 10) == SQLITE_TEXT)
            meta.original_performer = reinterpret_cast<const char*>(sqlite3_column_text(raw, 10));
        else
            assert(sqlite3_column_type(raw, 10) == SQLITE_NULL);
        if (sqlite3_column_type(raw, 11) == SQLITE_TEXT)
            meta.original_album = reinterpret_cast<const char*>(sqlite3_column_text(raw, 11));
        else
            assert(sqlite3_column_type(raw, 11) == SQLITE_NULL);
        if (sqlite3_column_type(raw, 12) == SQLITE_TEXT)
            meta.notes = reinterpret_cast<const char*>(sqlite3_column_text(raw, 12));
        else
            assert(sqlite3_column_type(raw, 12) == SQLITE_NULL);
        if (sqlite3_column_type(raw, 13) == SQLITE_TEXT)
        {
            meta.original_album_release_date = std::chrono::time_point_cast<std::chrono::days>
                (parse_iso8601(reinterpret_cast<const char*>(sqlite3_column_text(raw, 13))));
        }
        else
        {
            assert(sqlite3_column_type(raw, 13) == SQLITE_NULL);
        }
        if (sqlite3_column_type(raw, 14) == SQLITE_INTEGER)
            found.margin_width(sqlite3_column_int(raw, 14));
        else
            assert(sqlite3_column_type(raw, 14) == SQLITE_NULL);

        // --- Annotations ---
        // Pulled into local vectors first because annotations::load()
        // takes them by value and reseeds next_id_ in a single shot.
        // Handing rows in piecemeal would force us to manage next_id_
        // here, duplicating logic that already lives in the model.
        std::vector<model::text_box>  tbs;
        std::vector<model::connector> conns;
        {
            auto& sel_tb = prepared_statements_[statement::SELECT_SONG_TEXT_BOXES];
            sel_tb->reset();
            auto tb_raw = sel_tb->ptr();
            sqlite3_bind_int64(tb_raw, 1, row_id);
            while (sqlite3_step(tb_raw) == SQLITE_ROW)
            {
                model::text_box tb;
                tb.id   = sqlite3_column_int64(tb_raw, 0);
                tb.rect = QRectF(sqlite3_column_double(tb_raw, 1),
                                 sqlite3_column_double(tb_raw, 2),
                                 sqlite3_column_double(tb_raw, 3),
                                 sqlite3_column_double(tb_raw, 4));
                tb.text = QString::fromUtf8(
                    reinterpret_cast<const char*>(sqlite3_column_text(tb_raw, 5)));
                tbs.push_back(std::move(tb));
            }
        }
        {
            auto& sel_c = prepared_statements_[statement::SELECT_SONG_CONNECTORS];
            sel_c->reset();
            auto c_raw = sel_c->ptr();
            sqlite3_bind_int64(c_raw, 1, row_id);
            while (sqlite3_step(c_raw) == SQLITE_ROW)
            {
                model::connector c;
                c.id = sqlite3_column_int64(c_raw, 0);

                // Free-endpoint fallback positions; for glued
                // endpoints these are also written (as the last-known
                // resolved position), but we use them only when the
                // anchor refs are NULL or the referenced text box
                // doesn't exist in the just-loaded set.
                const QPointF s_xy(sqlite3_column_double(c_raw, 1),
                                   sqlite3_column_double(c_raw, 2));
                const QPointF e_xy(sqlite3_column_double(c_raw, 3),
                                   sqlite3_column_double(c_raw, 4));
                c.arrow_at_start = sqlite3_column_int(c_raw, 5) != 0;
                c.arrow_at_end   = sqlite3_column_int(c_raw, 6) != 0;

                // Reconstruct each endpoint.  Columns 7/8 are start
                // anchor (text_box_id, anchor_index), columns 9/10
                // are end.  NULL in either of the pair means "free."
                auto rebuild_endpoint =
                    [&](int id_col, int idx_col, const QPointF& fallback) {
                        if (sqlite3_column_type(c_raw, id_col)  == SQLITE_NULL ||
                            sqlite3_column_type(c_raw, idx_col) == SQLITE_NULL)
                        {
                            return model::connector_endpoint::make_free(fallback);
                        }
                        const std::uint64_t tb_id = sqlite3_column_int64(c_raw, id_col);
                        const unsigned idx = static_cast<unsigned>(
                            sqlite3_column_int(c_raw, idx_col));
                        return model::connector_endpoint::make_anchor(tb_id, idx);
                    };
                c.start = rebuild_endpoint(7, 8, s_xy);
                c.end   = rebuild_endpoint(9, 10, e_xy);
                conns.push_back(c);
            }
        }
        found.annotes().load(std::move(tbs), std::move(conns));
    }
    else
    {
        throw std::runtime_error("Song '"s + name + "' not found: " + error_msg(rc));
    }
    if (sqlite3_step(raw) != SQLITE_DONE)
        throw std::runtime_error("More than one song is named '"s + name + "'");
    tx.commit();
    return stored_song{ song_id{ row_id }, std::move(found) };
}

std::uint64_t database::time_signature_id(const model::time_signature& ts)
{
    assert(prepared_statements_.count(statement::INSERT_TIME_SIGNATURE) == 1);
    assert(prepared_statements_.count(statement::SELECT_TIME_SIGNATURE) == 1);
    auto& sel_ts = prepared_statements_[statement::SELECT_TIME_SIGNATURE];
    auto& ins_ts = prepared_statements_[statement::INSERT_TIME_SIGNATURE];
    sel_ts->reset();
    auto raw = sel_ts->ptr();
    sqlite3_bind_int(raw, 1, static_cast<int>(ts.kind()));
    sqlite3_bind_int(raw, 2, ts.count());
    auto rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW)
        return sqlite3_column_int64(raw, 0);
    ins_ts->reset();
    raw = ins_ts->ptr();
    sqlite3_bind_int(raw, 1, static_cast<int>(ts.kind()));
    sqlite3_bind_int(raw, 2, ts.count());
    rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a time signature: "s + error_msg(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

std::vector<stored_song> database::last_open_songs()
{
    std::vector<stored_song> result;
    prepared sel_ids(db_, "SELECT last_open_song_ids FROM metadata;");
    int rc = sqlite3_step(sel_ids.ptr());
    if (rc == SQLITE_ROW)
    {
        if (sqlite3_column_type(sel_ids.ptr(), 0) == SQLITE_TEXT)
        {
            prepared sel_name(db_, "SELECT name FROM song WHERE id = ?1;");
            std::istringstream in(reinterpret_cast<const char*>(sqlite3_column_text(sel_ids.ptr(), 0)));
            std::string id;
            while (std::getline(in, id, ',') && !id.empty())
            {
                sel_name.reset();
                sqlite3_bind_int64(sel_name.ptr(), 1, std::stol(id));
                int rc2 = sqlite3_step(sel_name.ptr());
                try
                {
                    if (rc2 == SQLITE_ROW)
                        result.push_back(select_song(reinterpret_cast<const char*>(sqlite3_column_text(sel_name.ptr(), 0))));
                    else
                        lgr()->warn("Unable to look up previously open song with id "s + id + ": " + error_msg(rc2));
                }
                catch (std::runtime_error& e)
                {
                    lgr()->warn("Error loading previously open song with id "s + id + ": " + e.what());
                }
            }
        }
    }
    else if (rc != SQLITE_DONE)
    {
        throw std::runtime_error("Error looking up last open songs: "s + error_msg(rc));
    }
    return result;
}

void database::last_open_songs(const std::vector<song_id>& opens)
{
    prepared ins(db_, "UPDATE metadata SET last_open_song_ids = ?1;");
    if (opens.empty())
    {
        sqlite3_bind_null(ins.ptr(), 1);
    }
    else
    {
        std::ostringstream out;
        for (const auto& cur : opens)
            out << static_cast<std::uint64_t>(cur) << ',';
        std::string text = out.str();
        if (!text.empty())
            text.pop_back();
        sqlite3_bind_text(ins.ptr(), 1, text.c_str(), text.length(), SQLITE_STATIC);
    }
    int rc = sqlite3_step(ins.ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Unable to set the last open songs in the database: "s + error_msg(rc));
}

std::string database::song_list_sort()
{
    prepared sel(db_, "SELECT song_list_sort FROM metadata;");
    if (sqlite3_step(sel.ptr()) == SQLITE_ROW)
        if (const unsigned char* txt = sqlite3_column_text(sel.ptr(), 0))
            return reinterpret_cast<const char*>(txt);
    // NULL / no row yet: the UI decides the default; "name" is the natural one.
    return "name";
}

void database::song_list_sort(const std::string& key)
{
    prepared upd(db_, "UPDATE metadata SET song_list_sort = ?1;");
    sqlite3_bind_text(upd.ptr(), 1, key.c_str(),
                      static_cast<int>(key.length()), SQLITE_TRANSIENT);
    int rc = sqlite3_step(upd.ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Unable to set the song list sort key: "s + error_msg(rc));
}

bool database::song_list_sort_descending()
{
    prepared sel(db_, "SELECT song_list_sort_desc FROM metadata;");
    if (sqlite3_step(sel.ptr()) == SQLITE_ROW)
        return sqlite3_column_int(sel.ptr(), 0) != 0;   // NULL reads as 0 (asc)
    return false;
}

void database::song_list_sort_descending(bool descending)
{
    prepared upd(db_, "UPDATE metadata SET song_list_sort_desc = ?1;");
    sqlite3_bind_int(upd.ptr(), 1, descending ? 1 : 0);
    int rc = sqlite3_step(upd.ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Unable to set the song list sort direction: "s + error_msg(rc));
}

std::vector<song_summary> database::song_summaries()
{
    // The metadata columns only — no chart data, and deliberately no ORDER BY:
    // the UI performs the sort, this just hands it the rows.  Missing values
    // come back as NULL, which we normalise to empty strings.
    std::vector<song_summary> result;
    prepared sel(db_,
        "SELECT name, authors, original_performer, original_album, "
        "original_album_release_date, notes, creation_time, modification_time "
        "FROM song;");

    auto text_at = [](sqlite3_stmt* s, int col) -> std::string {
        if (const unsigned char* t = sqlite3_column_text(s, col))
            return reinterpret_cast<const char*>(t);
        return {};
    };

    int rc;
    while ((rc = sqlite3_step(sel.ptr())) == SQLITE_ROW)
    {
        song_summary s;
        s.name         = text_at(sel.ptr(), 0);
        s.authors      = text_at(sel.ptr(), 1);
        s.performer    = text_at(sel.ptr(), 2);
        s.album        = text_at(sel.ptr(), 3);
        s.release_date = text_at(sel.ptr(), 4);
        s.notes        = text_at(sel.ptr(), 5);
        s.created      = text_at(sel.ptr(), 6);
        s.modified     = text_at(sel.ptr(), 7);
        result.push_back(std::move(s));
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve song summaries: "s + error_msg(rc));
    return result;
}

}
