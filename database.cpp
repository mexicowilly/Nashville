#include "database.hpp"
#include <cassert>
#include <cstdlib>
#include <chucho/log.hpp>

namespace
{

const char* schema = R"(
PRAGMA encoding = 'UTF-8';
PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS chord
(
    id INTEGER PRIMARY KEY NOT NULL,
    number INTEGER UNIQUE,
    mode INTEGER NOT NULL UNIQUE,
    flat_sharp BOOL UNIQUE,
    bass_note INTEGER UNIQUE,
    bass_note_step BOOL UNIQUE,
    extensions TEXT UNIQUE,
    is_staccato BOOL UNIQUE,
    is_diamond BOOL UNIQUE,
    duration INTEGER UNIQUE,
    is_tied BOOL UNIQUE,
    is_pushed BOOL UNIQUE
);

CREATE TABLE IF NOT EXISTS bar
(
    id INTEGER PRIMARY KEY NOT NULL,
    time_sig_id INTEGER,
    is_eol BOOL,
    section TEXT,
    FOREIGN KEY(time_sig_id) REFERENCES time_signature(id)
);

CREATE TABLE IF NOT EXISTS time_signature
(
    id INTEGER PRIMARY KEY NOT NULL,
    beat_type INTEGER CHECK(beat_type = 8 OR beat_type = 4 OR beat_type = 2) UNIQUE,
    count INTEGER UNIQUE
);

CREATE TABLE IF NOT EXISTS bar_chords
(
    id INTEGER PRIMARY KEY NOT NULL,
    chord_id INTEGER UNIQUE,
    bar_id INTEGER UNIQUE,
    chord_number INTEGER UNIQUE,
    FOREIGN KEY(chord_id) REFERENCES chord(id),
    FOREIGN KEY(bar_id) REFERENCES bar(id)
);

CREATE TABLE IF NOT EXISTS song
(
    id INTEGER PRIMARY KEY NOT NULL,
    name TEXT,
    key TEXT,
    time_sig_id INTEGER,
    bars_per_line INTEGER,
    beats_per_minute INTEGER,
    beats_unit INTEGER,
    FOREIGN KEY(time_sig_id) REFERENCES time_signature(id)
);

CREATE TABLE IF NOT EXISTS song_bars
(
    id INTEGER PRIMARY KEY NOT NULL,
    bar_id INTEGER UNIQUE,
    song_id INTEGER UNIQUE,
    bar_number INTEGER UNIQUE,
    FOREIGN KEY(bar_id) REFERENCES bar(id),
    FOREIGN KEY(song_id) REFERENCES song(id)
);

CREATE TABLE IF NOT EXISTS playlist
(
    id INTEGER PRIMARY KEY NOT NULL,
    name TEXT
);

CREATE TABLE IF NOT EXISTS playlist_songs
(
    id INTEGER PRIMARY KEY NOT NULL,
    song_id INTEGER UNIQUE,
    playlist_id INTEGER UNIQUE,
    song_number INTEGER UNIQUE,
    FOREIGN KEY(song_id) REFERENCES song(id),
    FOREIGN KEY(playlist_id) REFERENCES playlist(id)
);
)";

const char* SELECT_CHORD_SQL = R"(
SELECT * FROM chord WHERE
    number = ?1 AND
    mode = ?2 AND
    flat_sharp = ?3 AND
    bass_note = ?4 AND
    bass_note_step = ?5 AND
    extensions = ?6 AND
    is_staccato = ?7 AND
    is_diamond = ?8 AND
    duration = ?9 AND
    is_tied = ?10 AND
    is_pushed = ?11;
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
SELECT * FROM bar WHERE (time_sig = ?1, is_eol = ?2, section = ?3);
)";

const char* INSERT_BAR_SQL = R"(
INSERT INTO bar (time_sig_id, is_eol, section)
VALUES (?1, ?2, ?3)
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

const char* SELECT_BAR_CHORD_SQL = R"(
SELECT * FROM bar_chords WHERE (chord_id = ?1 AND
                                bar_id = ?2 AND
                                chord_number = ?3);
)";

const char* INSERT_BAR_CHORD_SQL = R"(
INSERT INTO bar_chords (chord_id, bar_id, chord_number)
VALUES (?1, ?2, ?3)
RETURNING id;
)";

const char* SELECT_SONG_SQL = R"(
SELECT * FROM song WHERE (name = ?1);
)";

const char* INSERT_SONG_SQL = R"(
INSERT INTO song (name,
                  key,
                  time_sig_id,
                  bars_per_line,
                  beats_per_minute,
                  beats_unit)
VALUES (?1, ?2, ?3, ?4, ?5, ?6)
RETURNING id;
)";

const char* SELECT_SONG_BAR_SQL = R"(
SELECT * FROM song_bars WHERE (bar_id = ?1 AND song_id = ?2 AND bar_number = ?3);
)";

const char* INSERT_SONG_BAR_SQL = R"(
INSERT INTO song_bars (bar_id, song_id, bar_number)
VALUES (?1, ?2, ?3)
RETURNING id;
)";

const char* SELECT_PLAYLIST_SQL = R"(
SELECT * FROM playlist WHERE (name = ?1);
)";

const char* INSERT_PLAYLIST_SQL = R"(
INSERT INTO playlist (name) VALUES (?1) RETURNING id;
)";

const char* SELECT_PLAYLIST_SONG_SQL = R"(
SELECT * FROM playlist_songs WHERE (song_id = ?1 AND
                                    playlist_id = ?2 AND
                                    song_number = ?3);
)";

const char* INSERT_PLAYLIST_SONG_SQL = R"(
INSERT INTO playlist_songs (song_id, playlist_id, song_number)
VALUES (?1, ?2, ?3)
RETURNING id;
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
    assert(rc == SQLITE_OK);
    if (rc != SQLITE_OK)
    {
        // THIS IS FATAL
        throw std::invalid_argument(std::string("Error preparing statement: ") + sql + " : " + sqlite3_errstr(rc));
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

database::database(const std::string& file_name)
{
    int rc = sqlite3_open_v2(file_name.c_str(),
                             &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXRESCODE,
                             nullptr);
    if (rc != SQLITE_OK)
    {
    }
    CHUCHO_DEBUG_L("Opened the database '" << file_name << "'");
    char* err;
    rc = sqlite3_exec(db_,
                      schema,
                      nullptr,
                      nullptr,
                      &err);
    assert(rc == SQLITE_OK);
    if (rc != SQLITE_OK)
    {
        // THIS IS FATAL
        CHUCHO_FATAL_L("The database schema contains errors: " << err);
        sqlite3_free(err);
        std::abort();
    }
    CHUCHO_DEBUG_L_STR("Successfully loaded the schema");
    try
    {
        prepared_statements_ =
        {
            { statement::SELECT_CHORD, std::make_shared<prepared>(db_, SELECT_CHORD_SQL) },
            { statement::INSERT_CHORD, std::make_shared<prepared>(db_, INSERT_CHORD_SQL) },
            { statement::SELECT_BAR, std::make_shared<prepared>(db_, SELECT_BAR_SQL) },
            { statement::INSERT_BAR, std::make_shared<prepared>(db_, INSERT_BAR_SQL) },
            { statement::SELECT_TIME_SIGNATURE, std::make_shared<prepared>(db_, SELECT_TIME_SIGNATURE_SQL) },
            { statement::INSERT_TIME_SIGNATURE, std::make_shared<prepared>(db_, INSERT_TIME_SIGNATURE_SQL) },
            { statement::SELECT_BAR_CHORD, std::make_shared<prepared>(db_, SELECT_BAR_CHORD_SQL) },
            { statement::INSERT_BAR_CHORD, std::make_shared<prepared>(db_, INSERT_BAR_CHORD_SQL) },
            { statement::SELECT_SONG, std::make_shared<prepared>(db_, SELECT_SONG_SQL) },
            { statement::INSERT_SONG, std::make_shared<prepared>(db_, INSERT_SONG_SQL) },
            { statement::SELECT_SONG_BAR, std::make_shared<prepared>(db_, SELECT_SONG_BAR_SQL) },
            { statement::INSERT_SONG_BAR, std::make_shared<prepared>(db_, INSERT_SONG_BAR_SQL) },
            { statement::SELECT_PLAYLIST, std::make_shared<prepared>(db_, SELECT_PLAYLIST_SQL) },
            { statement::INSERT_PLAYLIST, std::make_shared<prepared>(db_, INSERT_PLAYLIST_SQL) },
            { statement::SELECT_PLAYLIST_SONG, std::make_shared<prepared>(db_, SELECT_PLAYLIST_SONG_SQL) },
            { statement::INSERT_PLAYLIST_SONG, std::make_shared<prepared>(db_, INSERT_PLAYLIST_SONG_SQL) }
        };
    }
    catch (std::invalid_argument& e)
    {
        CHUCHO_FATAL_L_STR(e.what());
        std::abort();
    }
    CHUCHO_DEBUG_L_STR("Successfully created the prepared statements");
}

database::~database()
{
    prepared_statements_.clear();
    sqlite3_close(db_);
}

std::uint64_t database::insert_bar(const model::bar& b)
{
    assert(prepared_statements_.count(statement::SELECT_BAR) == 1);
    auto sel_b = prepared_statements_[statement::SELECT_BAR];
    sel_b->reset();
    auto raw = sel_b->ptr();
    if (b.time_sig())
        sqlite3_bind_int64(raw, 1, time_signature_id(*b.time_sig()));
    else
        sqlite3_bind_null(raw, 1);
    sqlite3_bind_int(raw, 2, b.is_eol());
    if (b.section())
        sqlite3_bind_text(raw, 3, b.section()->c_str(), b.section()->length(), SQLITE_STATIC);
    else
        sqlite3_bind_null(raw, 3);
    auto rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW)
        return sqlite3_column_int64(raw, 0);
    assert(prepared_statements_.count(statement::INSERT_BAR) == 1);
    auto ins_b = prepared_statements_[statement::INSERT_BAR];
    ins_b->reset();
    raw = ins_b->ptr();
    if (b.time_sig())
        sqlite3_bind_int64(raw, 1, time_signature_id(*b.time_sig()));
    else
        sqlite3_bind_null(raw, 1);
    sqlite3_bind_int(raw, 2, b.is_eol());
    if (b.section())
        sqlite3_bind_text(raw, 3, b.section()->c_str(), b.section()->length(), SQLITE_STATIC);
    else
        sqlite3_bind_null(raw, 3);
    rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error(std::string("Could not insert a bar: ") + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

std::uint64_t database::insert_chord(const model::chord& c)
{
    assert(prepared_statements_.count(statement::SELECT_CHORD) == 1);
    auto sel_c = prepared_statements_[statement::SELECT_CHORD];
    sel_c->reset();
    auto raw = sel_c->ptr();
    sqlite3_bind_int(raw, 1, c.number());
    sqlite3_bind_int(raw, 2, static_cast<int>(c.mode()));
    if (c.step())
        sqlite3_bind_int(raw, 3, static_cast<int>(*c.step()));
    else
        sqlite3_bind_null(raw, 3);
    if (c.bass_note())
        sqlite3_bind_int(raw, 4, *c.bass_note());
    else
        sqlite3_bind_null(raw, 4);
    if (c.bass_note_step())
        sqlite3_bind_int(raw, 5, static_cast<int>(*c.bass_note_step()));
    else
        sqlite3_bind_null(raw, 5);
    sqlite3_bind_text(raw, 6, c.extensions().c_str(), c.extensions().length(), SQLITE_STATIC);
    sqlite3_bind_int(raw, 7, c.is_staccato());
    sqlite3_bind_int(raw, 8, c.is_diamond());
    if (c.duration())
        sqlite3_bind_int(raw, 9, static_cast<int>(*c.duration()));
    else
        sqlite3_bind_null(raw, 9);
    sqlite3_bind_int(raw, 10, c.is_tied());
    sqlite3_bind_int(raw, 11, c.is_pushed());
    auto rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW)
        return sqlite3_column_int64(raw, 0);
    assert(prepared_statements_.count(statement::INSERT_CHORD) == 1);
    auto ins_c = prepared_statements_[statement::INSERT_CHORD];
    ins_c->reset();
    raw = ins_c->ptr();
    sqlite3_bind_int(raw, 1, c.number());
    sqlite3_bind_int(raw, 2, static_cast<int>(c.mode()));
    if (c.step())
        sqlite3_bind_int(raw, 3, static_cast<int>(*c.step()));
    else
        sqlite3_bind_null(raw, 3);
    if (c.bass_note())
        sqlite3_bind_int(raw, 4, *c.bass_note());
    else
        sqlite3_bind_null(raw, 4);
    if (c.bass_note_step())
        sqlite3_bind_int(raw, 5, static_cast<int>(*c.bass_note_step()));
    else
        sqlite3_bind_null(raw, 5);
    sqlite3_bind_text(raw, 6, c.extensions().c_str(), c.extensions().length(), SQLITE_STATIC);
    sqlite3_bind_int(raw, 7, c.is_staccato());
    sqlite3_bind_int(raw, 8, c.is_diamond());
    if (c.duration())
        sqlite3_bind_int(raw, 9, static_cast<int>(*c.duration()));
    else
        sqlite3_bind_null(raw, 9);
    sqlite3_bind_int(raw, 10, c.is_tied());
    sqlite3_bind_int(raw, 11, c.is_pushed());
    rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error(std::string("Could not insert a chord: ") + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

void database::insert_song(const model::song& s)
{
    assert(prepared_statements_.count(statement::INSERT_SONG) == 1);
    auto ins_s = prepared_statements_[statement::INSERT_SONG];
    ins_s->reset();
    auto raw = ins_s->ptr();
    sqlite3_bind_text(raw, 1, s.name().c_str(), s.name().length(), SQLITE_STATIC);
    sqlite3_bind_text(raw, 2, s.key().c_str(), s.key().length(), SQLITE_STATIC);
    sqlite3_bind_int64(raw, 3, time_signature_id(s.time_sig()));
    sqlite3_bind_int(raw, 4, s.bars_per_line());
    sqlite3_bind_int(raw, 5, std::get<0>(s.tempo()));
    sqlite3_bind_int(raw, 6, static_cast<int>(std::get<1>(s.tempo())));
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
    {
        // TODO: figure out the error handling
    }
    assert(sqlite3_column_count(raw) == 1);
    auto song_id = sqlite3_column_int64(raw, 0);
}

std::uint64_t database::time_signature_id(const model::time_signature& ts)
{
    assert(prepared_statements_.count(statement::SELECT_TIME_SIGNATURE) == 1);
    auto sel_ts = prepared_statements_[statement::SELECT_TIME_SIGNATURE];
    sel_ts.reset();
    auto raw = sel_ts->ptr();
    sqlite3_bind_int(raw, 1, static_cast<int>(ts.kind()));
    sqlite3_bind_int(raw, 2, ts.count());
    auto rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW)
        return sqlite3_column_int64(raw, 0);
    assert(prepared_statements_.count(statement::INSERT_TIME_SIGNATURE) == 1);
    auto ins_ts = prepared_statements_[statement::INSERT_TIME_SIGNATURE];
    ins_ts.reset();
    raw = ins_ts->ptr();
    sqlite3_bind_int(raw, 1, static_cast<int>(ts.kind()));
    sqlite3_bind_int(raw, 2, ts.count());
    rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error(std::string("Could not insert a time signature: ") + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

}
