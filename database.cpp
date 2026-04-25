#include "database.hpp"
#include <cassert>
#include <cstdlib>
#include <chucho/log.hpp>
#include <algorithm>

using namespace std::string_literals;

namespace
{

const char* schema = R"(
PRAGMA encoding = 'UTF-8';
PRAGMA foreign_keys = ON;

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
    FOREIGN KEY(chord_id) REFERENCES chord(id),
    FOREIGN KEY(bar_id) REFERENCES bar(id)
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
    FOREIGN KEY(playlist_id) REFERENCES playlist(id) ON DELETE CASCADE;
);

CREATE INDEX IF NOT EXISTS playlist_id_index ON playlist_songs(playlist_id, song_index);
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
       time_signature.count
FROM bar
LEFT JOIN time_signature ON time_signature.id = bar.time_sig_id
WHERE bar.id = ?1;
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

const char* INSERT_BAR_CHORD_SQL = R"(
INSERT INTO bar_chords (chord_id, bar_id, chord_index)
VALUES (?1, ?2, ?3)
RETURNING id;
)";

const char* SELECT_SONGS_SQL = R"(
SELECT song.id,
       song.name,
       song.key,
       song.bars_per_line,
       song.beats_per_minute,
       song.beats_unit,
       time_signature.beat_type,
       time_signature.count
FROM song
LEFT JOIN time_signature ON time_signature.id = song.time_sig_id;
)";

const char* SELECT_SONG_SQL = R"(
SELECT song.id,
       song.key,
       song.bars_per_line,
       song.beats_per_minute,
       song.beats_unit,
       time_signature.beat_type,
       time_signature.count
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
                  beats_unit)
VALUES (?1, ?2, ?3, ?4, ?5, ?6)
RETURNING id;
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
VALUES (?1, ?2, ?3)
RETURNING id;
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

//const char* REMOVE_BAR_CHORDS_SQL = R"(
//DELETE FROM bar_chords WHERE bar_id = ?1;
//)";

const char* REMOVE_SONG_SQL = R"(
DELETE FROM song WHERE id = ?1;
)";

const char* REMOVE_BAR_SQL = R"(
DELETE FROM bar WHERE id = ?1;
)";

const char* REMOVE_PLAYLIST_SONGS_SQL = R"(
DELETE FROM playlist_songs WHERE playlist_id = ?1;
)";

const char* REMOVE_PLAYLIST_SQL = R"(
DELETE FROM playlist WHERE name = ?1 RETURNING id;
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
        throw std::invalid_argument("Error preparing statement: '"s + sql + "': " + sqlite3_errstr(rc));
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

database::database()
    : database(":memory:")
{
}

database::database(const std::filesystem::path& file_name)
{
    int rc = sqlite3_open_v2(file_name.c_str(),
                             &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_EXRESCODE,
                             nullptr);
    if (rc != SQLITE_OK)
        throw std::runtime_error("Unable to open the database '"s + file_name.c_str() + "' " + sqlite3_errstr(rc));
    CHUCHO_DEBUG_L("Opened the database '" << file_name << "'");
    char* err;
    rc = sqlite3_exec(db_,
                      schema,
                      nullptr,
                      nullptr,
                      &err);
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
            { statement::SELECT_CHORD_BY_ID, std::make_shared<prepared>(db_, SELECT_CHORD_BY_ID_SQL) },
            { statement::INSERT_CHORD, std::make_shared<prepared>(db_, INSERT_CHORD_SQL) },
            { statement::SELECT_BAR, std::make_shared<prepared>(db_, SELECT_BAR_SQL) },
            { statement::INSERT_BAR, std::make_shared<prepared>(db_, INSERT_BAR_SQL) },
            { statement::SELECT_CHORDS_BY_BAR, std::make_shared<prepared>(db_, SELECT_CHORDS_BY_BAR_SQL) },
            { statement::SELECT_TIME_SIGNATURE, std::make_shared<prepared>(db_, SELECT_TIME_SIGNATURE_SQL) },
            { statement::INSERT_TIME_SIGNATURE, std::make_shared<prepared>(db_, INSERT_TIME_SIGNATURE_SQL) },
            { statement::INSERT_BAR_CHORD, std::make_shared<prepared>(db_, INSERT_BAR_CHORD_SQL) },
            { statement::SELECT_SONG_ID, std::make_shared<prepared>(db_, SELECT_SONG_ID_SQL) },
            { statement::INSERT_SONG, std::make_shared<prepared>(db_, INSERT_SONG_SQL) },
            { statement::SELECT_SONG_BARS, std::make_shared<prepared>(db_, SELECT_SONG_BARS_SQL) },
            { statement::INSERT_SONG_BAR, std::make_shared<prepared>(db_, INSERT_SONG_BAR_SQL) },
            { statement::INSERT_PLAYLIST, std::make_shared<prepared>(db_, INSERT_PLAYLIST_SQL) },
            { statement::SELECT_PLAYLIST_SONGS, std::make_shared<prepared>(db_, SELECT_PLAYLIST_SONGS_SQL) },
            { statement::INSERT_PLAYLIST_SONG, std::make_shared<prepared>(db_, INSERT_PLAYLIST_SONG_SQL) },
            { statement::SELECT_SONG_NAMES, std::make_shared<prepared>(db_, SELECT_SONG_NAMES_SQL) },
            { statement::SELECT_SONG, std::make_shared<prepared>(db_, SELECT_SONG_SQL) },
            { statement::SELECT_PLAYLIST_NAMES, std::make_shared<prepared>(db_, SELECT_PLAYLIST_NAMES_SQL) },
            { statement::SELECT_PLAYLIST_ID, std::make_shared<prepared>(db_, SELECT_PLAYLIST_ID_SQL) },
            { statement::SELECT_CHORD_NOT_IN_BAR, std::make_shared<prepared>(db_, SELECT_CHORD_NOT_IN_BAR_SQL) },
            { statement::REMOVE_CHORD, std::make_shared<prepared>(db_, REMOVE_CHORD_SQL) },
            //{ statement::REMOVE_BAR_CHORDS, std::make_shared<prepared>(db_, REMOVE_BAR_CHORDS_SQL) },
            { statement::REMOVE_BAR, std::make_shared<prepared>(db_, REMOVE_BAR_SQL) },
            { statement::REMOVE_SONG, std::make_shared<prepared>(db_, REMOVE_SONG_SQL) },
            { statement::REMOVE_PLAYLIST_SONGS, std::make_shared<prepared>(db_, REMOVE_PLAYLIST_SONGS_SQL) }
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
    assert(prepared_statements_.count(statement::INSERT_BAR) == 1);
    auto ins_b = prepared_statements_[statement::INSERT_BAR];
    ins_b->reset();
    auto raw = ins_b->ptr();
    if (b.time_sig())
        sqlite3_bind_int64(raw, 1, time_signature_id(*b.time_sig()));
    sqlite3_bind_int(raw, 2, b.is_eol());
    if (b.section())
        sqlite3_bind_text(raw, 3, b.section()->c_str(), b.section()->length(), SQLITE_STATIC);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a bar: "s + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

std::uint64_t database::insert_bar_chord(std::uint64_t bar_id, std::uint64_t chord_id, unsigned index)
{
    assert(prepared_statements_.count(statement::INSERT_BAR_CHORD) == 1);
    auto ins_bc = prepared_statements_[statement::INSERT_BAR_CHORD];
    ins_bc->reset();
    auto raw = ins_bc->ptr();
    sqlite3_bind_int(raw, 1, chord_id);
    sqlite3_bind_int(raw, 2, bar_id);
    sqlite3_bind_int(raw, 3, index);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a bar chord: "s + sqlite3_errstr(rc));
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
        return sqlite3_column_int64(raw, 0);
    }
    assert(prepared_statements_.count(statement::INSERT_CHORD) == 1);
    auto ins_c = prepared_statements_[statement::INSERT_CHORD];
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
        throw std::runtime_error("Could not insert a chord: "s + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

void database::insert_playlist(const model::playlist& pl)
{
    assert(prepared_statements_.count(statement::INSERT_PLAYLIST) == 1);
    assert(prepared_statements_.count(statement::SELECT_SONG_ID) == 1);
    assert(prepared_statements_.count(statement::INSERT_PLAYLIST_SONG) == 1);
    auto ins_pl = prepared_statements_[statement::INSERT_PLAYLIST];
    auto sel_sid = prepared_statements_[statement::SELECT_SONG_ID];
    auto ins_ps = prepared_statements_[statement::INSERT_PLAYLIST_SONG];
    auto raw = ins_pl->ptr();
    int rc;
    for (int i = 0; i < 2; i++)
    {
        ins_pl->reset();
        sqlite3_bind_text(raw, 1, pl.name().c_str(), pl.name().length(), SQLITE_STATIC);
        rc = sqlite3_step(raw);
        if (rc == SQLITE_CONSTRAINT_UNIQUE)
        {
            CHUCHO_DEBUG_L("Replacing the playlist '" << pl.name() << "'");
            remove_playlist(pl.name());
        }
        else
        {
            break;
        }
    }
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert playlist '"s + pl.name() + "':" + sqlite3_errstr(rc));
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
                        pl.name() + "', song '" + n + "': " + sqlite3_errstr(rc2));
            }
        }
        else
        {
            throw std::runtime_error("Problem looking up a song named '"s + n + "':" + sqlite3_errstr(rc2));
        }
    }
}

void database::insert_song(const model::song& s)
{
    assert(prepared_statements_.count(statement::INSERT_SONG) == 1);
    auto ins_s = prepared_statements_[statement::INSERT_SONG];
    auto raw = ins_s->ptr();
    int rc;
    for (int i = 0; i < 2; i++)
    {
        ins_s->reset();
        sqlite3_bind_text(raw, 1, s.name().c_str(), s.name().length(), SQLITE_STATIC);
        sqlite3_bind_text(raw, 2, s.key().c_str(), s.key().length(), SQLITE_STATIC);
        sqlite3_bind_int64(raw, 3, time_signature_id(s.time_sig()));
        sqlite3_bind_int(raw, 4, s.bars_per_line());
        sqlite3_bind_int(raw, 5, std::get<0>(s.tempo()));
        sqlite3_bind_int(raw, 6, static_cast<int>(std::get<1>(s.tempo())));
        rc = sqlite3_step(raw);
        if (rc == SQLITE_CONSTRAINT_UNIQUE)
        {
            CHUCHO_DEBUG_L("Replacing the song '" << s.name() << "'");
            remove_song(s.name());
        }
        else
        {
            break;
        }
    }
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Unable to insert song '"s + s.name() + "': " + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    auto song_id = sqlite3_column_int64(raw, 0);
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
}

std::uint64_t database::insert_song_bar(std::uint64_t song_id, std::uint64_t bar_id, unsigned index)
{
    assert(prepared_statements_.count(statement::INSERT_SONG_BAR) == 1);
    auto ins_sb = prepared_statements_[statement::INSERT_SONG_BAR];
    ins_sb->reset();
    auto raw = ins_sb->ptr();
    sqlite3_bind_int(raw, 1, bar_id);
    sqlite3_bind_int(raw, 2, song_id);
    sqlite3_bind_int(raw, 3, index);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a song bar: "s + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

void database::maybe_remove_chord(std::uint64_t bar_id, std::uint64_t chord_id)
{
    assert(prepared_statements_.count(statement::SELECT_CHORD_NOT_IN_BAR) == 1);
    auto sel_c = prepared_statements_[statement::SELECT_CHORD_NOT_IN_BAR];
    sel_c->reset();
    sqlite3_bind_int64(sel_c->ptr(), 1, chord_id);
    sqlite3_bind_int64(sel_c->ptr(), 2, bar_id);
    auto rc = sqlite3_step(sel_c->ptr());
    if (rc == SQLITE_ROW)
        return;
    assert(prepared_statements_.count(statement::REMOVE_CHORD) == 1);
    auto rem_c = prepared_statements_[statement::REMOVE_CHORD];
    rem_c->reset();
    sqlite3_bind_int64(rem_c->ptr(), 1, chord_id);
    rc = sqlite3_step(rem_c->ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could remove a chord: "s + sqlite3_errstr(rc));
}

void database::move_to_file(const std::filesystem::path& file_name)
{
    database other(file_name);
    auto back = sqlite3_backup_init(other.db_, "main", db_, "main");
    if (back == nullptr)
    {
        throw std::runtime_error("Could not move database to file '"s +
                                 file_name.string() + "': " +
                                 sqlite3_errstr(sqlite3_errcode(other.db_)));
    }
    auto rc = sqlite3_backup_step(back, -1);
    sqlite3_backup_finish(back);
    if (rc != SQLITE_DONE)
    {
        throw std::runtime_error("Could not move database to file '"s +
                                 file_name.string() + "': " + sqlite3_errstr(rc));
    }
    *this = other;
    // Prevent the closing of the database in other
    other.db_ = nullptr;
}

void database::remove_playlist(const std::string& pl)
{
    assert(prepared_statements_.count(statement::REMOVE_PLAYLIST) == 1);
    assert(prepared_statements_.count(statement::REMOVE_PLAYLIST_SONGS) == 1);
    auto rm_pl = prepared_statements_[statement::REMOVE_PLAYLIST];
    auto rm_pls = prepared_statements_[statement::REMOVE_PLAYLIST_SONGS];
    rm_pl->reset();
    sqlite3_bind_text(rm_pl->ptr(), 1, pl.c_str(), pl.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(rm_pl->ptr());
    if (rc == SQLITE_ROW)
    {
        rm_pls->reset();
        sqlite3_bind_int64(rm_pls->ptr(), 1, sqlite3_column_int64(rm_pl->ptr(), 0));
        rc = sqlite3_step(rm_pls->ptr());
        if (rc != SQLITE_DONE)
            throw std::runtime_error("Could not remove song references for playlist '"s + pl + "': " + sqlite3_errstr(rc));
    }
    else
    {
        throw std::runtime_error("Could not remove playlist '"s + pl + "': " + sqlite3_errstr(rc));
    }
}

void database::remove_song(const std::string& s)
{
    assert(prepared_statements_.count(statement::SELECT_SONG_ID) == 1);
    assert(prepared_statements_.count(statement::SELECT_CHORDS_BY_BAR) == 1);
    assert(prepared_statements_.count(statement::REMOVE_BAR) == 1);
    assert(prepared_statements_.count(statement::REMOVE_SONG) == 1);
    assert(prepared_statements_.count(statement::REMOVE_PLAYLIST_SONGS) == 1);
    auto sel_s = prepared_statements_[statement::SELECT_SONG_ID];
    auto sel_cs = prepared_statements_[statement::SELECT_CHORDS_BY_BAR];
    auto rem_b = prepared_statements_[statement::REMOVE_BAR];
    sel_s->reset();
    sqlite3_bind_text(sel_s->ptr(), 1, s.c_str(), s.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(sel_s->ptr());
    auto song_id = sqlite3_column_int64(sel_s->ptr(), 0);
    if (rc == SQLITE_ROW)
    {
        assert(prepared_statements_.count(statement::SELECT_SONG_BARS) == 1);
        auto sel_bids = prepared_statements_[statement::SELECT_SONG_BARS];
        sel_bids->reset();
        sqlite3_bind_int64(sel_bids->ptr(), 1, song_id);
        auto rc2 = sqlite3_step(sel_bids->ptr());
        while (rc2 == SQLITE_ROW)
        {
            auto bar_id = sqlite3_column_int64(sel_bids->ptr(), 0);
            // Now maybe_remove_chord on the bar. If the chord is removed, then all of
            // its bar_chords will also be removed thanks to cascading
            sel_cs->reset();
            sqlite3_bind_int64(sel_cs->ptr(), 1, bar_id);
            auto rc3 = sqlite3_step(sel_cs->ptr());
            while (rc3 == SQLITE_ROW)
            {
                maybe_remove_chord(bar_id, sqlite3_column_int64(sel_cs->ptr(), 0));
                rc3 = sqlite3_step(sel_cs->ptr());
            }
            if (rc3 != SQLITE_DONE)
                throw std::runtime_error("Could not remove chords:"s + sqlite3_errstr(rc3));
            // Then remove all bar_chords for this bar
            //rem_bcs->reset();
            //sqlite3_bind_int64(rem_bcs->ptr(), 1, bar_id);
            //rc3 = sqlite3_step(rem_bcs->ptr());
            //if (rc3 != SQLITE_DONE)
                //throw std::runtime_error("Could not remove bar:"s + sqlite3_errstr(rc3));

            // Then remove the bar itself
            rem_b->reset();
            sqlite3_bind_int64(rem_b->ptr(), 1, bar_id);
            rc3 = sqlite3_step(rem_b->ptr());
            if (rc3 != SQLITE_DONE)
                throw std::runtime_error("Could not remove bar:"s + sqlite3_errstr(rc3));
            rc2 = sqlite3_step(sel_bids->ptr());
        }
        if (rc2 != SQLITE_DONE)
            throw std::runtime_error("Could not remove song '"s + s + "': " + sqlite3_errstr(rc2));
        // Remove the song
        auto rem_s = prepared_statements_[statement::REMOVE_SONG];
        rem_s->reset();
        sqlite3_bind_int64(rem_s->ptr(), 1, song_id);
        rc = sqlite3_step(rem_s->ptr());
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not remove song '"s + s + "': " + sqlite3_errstr(rc));
    // Remove the song from any playlists that have it
    auto rem_ps = prepared_statements_[statement::REMOVE_PLAYLIST_SONGS];
    rem_ps->reset();
    sqlite3_bind_int64(rem_ps->ptr(), 1, song_id);
    rc = sqlite3_step(rem_ps->ptr());
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not remove song '"s + s + "' from playlists: " + sqlite3_errstr(rc));
}

std::vector<model::bar> database::select_bars(std::uint64_t song_id)
{
    std::vector<model::bar> bars;
    assert(prepared_statements_.count(statement::SELECT_SONG_BARS) == 1);
    assert(prepared_statements_.count(statement::SELECT_BAR) == 1);
    auto sel_bs = prepared_statements_[statement::SELECT_SONG_BARS];
    sel_bs->reset();
    auto raw = sel_bs->ptr();
    auto sel_b = prepared_statements_[statement::SELECT_BAR];
    sqlite3_bind_int64(raw, 1, song_id);
   auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        sel_b->reset();
        auto bar_id = sqlite3_column_int64(raw, 0);
        sqlite3_bind_int64(sel_b->ptr(), 1, bar_id);
        auto rc2 = sqlite3_step(sel_b->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error("Could not look up bar by ID: "s + sqlite3_errstr(rc2));
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
        bar.chords(select_chords(bar_id));
        bars.push_back(bar);
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve bars: "s + sqlite3_errstr(rc));
    return bars;
}

std::vector<model::chord> database::select_chords(std::uint64_t bar_id)
{
    std::vector<model::chord> chords;
    assert(prepared_statements_.count(statement::SELECT_CHORDS_BY_BAR) == 1);
    assert(prepared_statements_.count(statement::SELECT_CHORD_BY_ID) == 1);
    auto sel_cs = prepared_statements_[statement::SELECT_CHORDS_BY_BAR];
    sel_cs->reset();
    auto raw = sel_cs->ptr();
    sqlite3_bind_int64(raw, 1, bar_id);
    auto sel_c = prepared_statements_[statement::SELECT_CHORD_BY_ID];
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        model::chord ch;
        sel_c->reset();
        sqlite3_bind_int64(sel_c->ptr(), 1, sqlite3_column_int64(raw, 0));
        auto rc2 = sqlite3_step(sel_c->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error("Could not look up chord by ID: "s + sqlite3_errstr(rc2));
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
        throw std::runtime_error("Could not retrieve chords: "s + sqlite3_errstr(rc));
    return chords;
}

model::playlist database::select_playlist(const std::string& name)
{
    model::playlist p(name);
    assert(prepared_statements_.count(statement::SELECT_PLAYLIST_ID) == 1);
    auto sel_p = prepared_statements_[statement::SELECT_PLAYLIST_ID];
    sel_p->reset();
    sqlite3_bind_text(sel_p->ptr(), 1, name.c_str(), name.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(sel_p->ptr());
    if (rc == SQLITE_ROW)
    {
        assert(prepared_statements_.count(statement::SELECT_PLAYLIST_SONGS) == 1);
        auto sel_ps = prepared_statements_[statement::SELECT_PLAYLIST_SONGS];
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
            throw std::runtime_error("Error retrieving playlist songs: "s + sqlite3_errstr(rc));
    }
    else if (rc != SQLITE_DONE)
    {
        throw std::runtime_error("Error retrieving playlist: "s + sqlite3_errstr(rc));
    }
    return p;
}

std::vector<std::string> database::select_playlist_names()
{
    std::vector<std::string> names;
    assert(prepared_statements_.count(statement::SELECT_PLAYLIST_NAMES) == 1);
    auto sel_p = prepared_statements_[statement::SELECT_PLAYLIST_NAMES];
    sel_p->reset();
    auto raw = sel_p->ptr();
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        names.push_back(reinterpret_cast<const char*>(sqlite3_column_text(raw, 0)));
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve playlist names: "s + sqlite3_errstr(rc));
    return names;
}

std::vector<std::string> database::select_song_names()
{
    std::vector<std::string> names;
    assert(prepared_statements_.count(statement::SELECT_SONG_NAMES) == 1);
    auto sel_s = prepared_statements_[statement::SELECT_SONG_NAMES];
    sel_s->reset();
    auto raw = sel_s->ptr();
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        names.push_back(reinterpret_cast<const char*>(sqlite3_column_text(raw, 0)));
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error("Could not retrieve song names: "s + sqlite3_errstr(rc));
    return names;
}

model::song database::select_song(const std::string& name)
{
    model::song found(name);
    assert(prepared_statements_.count(statement::SELECT_SONG) == 1);
    auto sel_s = prepared_statements_[statement::SELECT_SONG];
    sel_s->reset();
    auto raw = sel_s->ptr();
    sqlite3_bind_text(raw, 1, name.c_str(), name.length(), SQLITE_STATIC);
    auto rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW)
    {
        found.key(reinterpret_cast<const char*>(sqlite3_column_text(raw, 1)));
        found.bars_per_line(sqlite3_column_int(raw, 2));
        found.tempo(std::make_tuple(sqlite3_column_int(raw, 3), static_cast<model::chord::time>(sqlite3_column_int(raw, 4))));
        found.bars(select_bars(sqlite3_column_int64(raw, 0)));
        model::time_signature ts;
        ts.kind(static_cast<model::time_signature::beat_type>(sqlite3_column_int(raw, 5)))
          .count(sqlite3_column_int(raw, 6));
        found.time_sig(ts);
    }
    else
    {
        throw std::runtime_error("Song '"s + name + "' not found: " + sqlite3_errstr(rc));
    }
    if (sqlite3_step(raw) != SQLITE_DONE)
        throw std::runtime_error("More than one song is named '"s + name + "'");
    return found;
}

std::uint64_t database::time_signature_id(const model::time_signature& ts)
{
    assert(prepared_statements_.count(statement::SELECT_TIME_SIGNATURE) == 1);
    auto sel_ts = prepared_statements_[statement::SELECT_TIME_SIGNATURE];
    sel_ts->reset();
    auto raw = sel_ts->ptr();
    sqlite3_bind_int(raw, 1, static_cast<int>(ts.kind()));
    sqlite3_bind_int(raw, 2, ts.count());
    auto rc = sqlite3_step(raw);
    if (rc == SQLITE_ROW)
        return sqlite3_column_int64(raw, 0);
    assert(prepared_statements_.count(statement::INSERT_TIME_SIGNATURE) == 1);
    auto ins_ts = prepared_statements_[statement::INSERT_TIME_SIGNATURE];
    ins_ts->reset();
    raw = ins_ts->ptr();
    sqlite3_bind_int(raw, 1, static_cast<int>(ts.kind()));
    sqlite3_bind_int(raw, 2, ts.count());
    rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error("Could not insert a time signature: "s + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

}
