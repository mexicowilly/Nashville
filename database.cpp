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
    beat_type INTEGER CHECK(beat_type = 8 OR beat_type = 4 OR beat_type = 2),
    count INTEGER
);

CREATE TABLE IF NOT EXISTS bar_chords
(
    id INTEGER PRIMARY KEY NOT NULL,
    chord_id INTEGER NOT NULL,
    bar_id INTEGER NOT NULL,
    chord_number INTEGER NOT NULL,
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
    bar_id INTEGER NOT NULL,
    song_id INTEGER NOT NULL,
    bar_number INTEGER NOT NULL,
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
    song_id INTEGER NOT NULL,
    playlist_id INTEGER NOT NULL,
    song_number INTEGER NOT NULL,
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
SELECT * FROM bar WHERE (id = ?1);
)";

const char* INSERT_BAR_SQL = R"(
INSERT INTO bar (time_sig_id, is_eol, section)
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
    try
    {
        prepared_statements_ =
        {
            { statement::SELECT_CHORD, prepared(db_, SELECT_CHORD_SQL) },
            { statement::INSERT_CHORD, prepared(db_, INSERT_CHORD_SQL) },
            { statement::SELECT_BAR, prepared(db_, SELECT_BAR_SQL) },
            { statement::INSERT_BAR, prepared(db_, INSERT_BAR_SQL) }
        };
    }
    catch (std::invalid_argument& e)
    {
        CHUCHO_FATAL_L(e.what());
        std::abort();
    }
}

database::~database()
{
    prepared_statements_.clear();
    sqlite3_close(db_);
}

}
