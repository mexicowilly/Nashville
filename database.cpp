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
    chord_index INTEGER UNQIUE,
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
    index INTEGER,
    FOREIGN KEY(time_sig_id) REFERENCES time_signature(id)
);

CREATE TABLE IF NOT EXISTS song_bars
(
    id INTEGER PRIMARY KEY NOT NULL,
    bar_id INTEGER UNIQUE,
    song_id INTEGER UNIQUE,
    index INTEGER UNIQUE,
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
    song_index INTEGER UNIQUE,
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

const char* SELECT_CHORDS_BY_BAR_SQL = R"(
SELECT chord_id FROM bar_chords WHERE bar_id = ?1 ORDER BY index;
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
SELECT * FROM bar WHERE id = ?1;
)";

const char* INSERT_BAR_SQL = R"(
INSERT INTO bar (time_sig_id, is_eol, section)
VALUES (?1, ?2, ?3)
RETURNING id;
)";

const char* SELECT_TIME_SIGNATURE_SQL = R"(
SELECT * FROM time_signature WHERE (beat_type = ?1 AND count = ?2);
)";

const char* SELECT_TIME_SIGNATURE_BY_ID_SQL = R"(
SELECT (beat_type, count) FROM time_signature WHERE (id = ?1);
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
SELECT * FROM song ORDER BY index;
)";

const char* INSERT_SONG_SQL = R"(
INSERT INTO song (name,
                  key,
                  time_sig_id,
                  bars_per_line,
                  beats_per_minute,
                  beats_unit,
                  index)
VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)
RETURNING id;
)";

const char* SELECT_SONG_BARS_SQL = R"(
SELECT bar_id FROM song_bars WHERE song_id = ?1 ORDER BY index;
)";

const char* INSERT_SONG_BAR_SQL = R"(
INSERT INTO song_bars (bar_id, song_id, index)
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
                                    song_index = ?3);
)";

const char* INSERT_PLAYLIST_SONG_SQL = R"(
INSERT INTO playlist_songs (song_id, playlist_id, song_index)
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
            { statement::SELECT_CHORD_BY_ID, std::make_shared<prepared>(db_, SELECT_CHORD_BY_ID_SQL) },
            { statement::INSERT_CHORD, std::make_shared<prepared>(db_, INSERT_CHORD_SQL) },
            { statement::SELECT_BAR, std::make_shared<prepared>(db_, SELECT_BAR_SQL) },
            { statement::INSERT_BAR, std::make_shared<prepared>(db_, INSERT_BAR_SQL) },
            { statement::SELECT_CHORDS_BY_BAR, std::make_shared<prepared>(db_, SELECT_CHORDS_BY_BAR_SQL) },
            { statement::SELECT_TIME_SIGNATURE, std::make_shared<prepared>(db_, SELECT_TIME_SIGNATURE_SQL) },
            { statement::SELECT_TIME_SIGNATURE_BY_ID, std::make_shared<prepared>(db_, SELECT_TIME_SIGNATURE_BY_ID_SQL) },
            { statement::INSERT_TIME_SIGNATURE, std::make_shared<prepared>(db_, INSERT_TIME_SIGNATURE_SQL) },
            { statement::INSERT_BAR_CHORD, std::make_shared<prepared>(db_, INSERT_BAR_CHORD_SQL) },
            { statement::SELECT_SONGS, std::make_shared<prepared>(db_, SELECT_SONGS_SQL) },
            { statement::INSERT_SONG, std::make_shared<prepared>(db_, INSERT_SONG_SQL) },
            { statement::SELECT_SONG_BARS, std::make_shared<prepared>(db_, SELECT_SONG_BARS_SQL) },
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
    assert(prepared_statements_.count(statement::INSERT_BAR) == 1);
    auto ins_b = prepared_statements_[statement::INSERT_BAR];
    ins_b->reset();
    auto raw = ins_b->ptr();
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
    if (rc != SQLITE_ROW)
        throw std::runtime_error(std::string("Could not insert a bar: ") + sqlite3_errstr(rc));
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
        throw std::runtime_error(std::string("Could not insert a bar chord: ") + sqlite3_errstr(rc));
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

void database::insert_song(const model::song& s, unsigned idx)
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
    sqlite3_bind_int(raw, 7, idx);
    auto rc = sqlite3_step(raw);
    if (rc != SQLITE_ROW)
        throw std::runtime_error(std::string("Unable to insert song '") + s.name() + "': " + sqlite3_errstr(rc));
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

void database::insert_songs(const std::vector<model::song>& songs)
{
    for (unsigned i = 0; i < songs.size(); i++)
        insert_song(songs[i], i);
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
        throw std::runtime_error(std::string("Could not insert a song bar: ") + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

std::vector<model::bar> database::select_bars(std::uint64_t song_id)
{
    std::vector<model::bar> bars;
    assert(prepared_statements_.count(statement::SELECT_SONG_BARS) == 1);
    assert(prepared_statements_.count(statement::SELECT_BAR) == 1);
    assert(prepared_statements_.count(statement::SELECT_TIME_SIGNATURE_BY_ID) == 1);
    auto sel_bs = prepared_statements_[statement::SELECT_SONG_BARS];
    sel_bs->reset();
    auto raw = sel_bs->ptr();
    auto sel_b = prepared_statements_[statement::SELECT_BAR];
    sqlite3_bind_int64(raw, 1, song_id);
    auto sel_ts = prepared_statements_[statement::SELECT_TIME_SIGNATURE_BY_ID];
    auto rc = sqlite3_step(raw);
    while (rc == SQLITE_ROW)
    {
        model::bar bar;
        sel_b->reset();
        auto bar_id = sqlite3_column_int64(raw, 0);
        sqlite3_bind_int64(sel_b->ptr(), 1, bar_id);
        auto rc2 = sqlite3_step(sel_b->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error(std::string("Could not look up bar by ID: ") + sqlite3_errstr(rc2));
        sel_ts->reset();
        sqlite3_bind_int64(sel_ts->ptr(), 1, sqlite3_column_int64(sel_b->ptr(), 1));
        auto rc3 = sqlite3_step(sel_ts->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error(std::string("Could not find a time signature:") + sqlite3_errstr(rc2));
        model::time_signature ts;
        ts.kind(static_cast<model::time_signature::beat_type>(sqlite3_column_int(sel_ts->ptr(), 1)))
          .count(sqlite3_column_int(sel_ts->ptr(), 2));
        bar.time_sig(ts);
        bar.is_eol(sqlite3_column_int(sel_b->ptr(), 1));
        if (sqlite3_column_type(sel_b->ptr(), 3) == SQLITE_TEXT)
            bar.section(reinterpret_cast<const char*>(sqlite3_column_text(sel_b->ptr(), 3)));
        else
            assert(sqlite3_column_type(sel_b->ptr(), 3) == SQLITE_NULL);
        bar.chords(select_chords(bar_id));
        bars.push_back(bar);
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error(std::string("Could not retrieve bars: ") + sqlite3_errstr(rc));
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
        sel_c->reset();
        sqlite3_bind_int64(sel_c->ptr(), 1, sqlite3_column_int64(raw, 0));
        auto rc2 = sqlite3_step(sel_c->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error(std::string("Could not look up chord by ID: ") + sqlite3_errstr(rc2));
        model::chord ch;
        ch.number(sqlite3_column_int(raw, 1))
          .mode(static_cast<model::chord::type>(sqlite3_column_int(raw, 2)));
        if (sqlite3_column_type(raw, 3) == SQLITE_INTEGER)
            ch.step(static_cast<model::chord::flat_sharp>(sqlite3_column_int(raw, 3)));
        else
            assert(sqlite3_column_type(raw, 3) == SQLITE_NULL);
        if (sqlite3_column_type(raw, 4) == SQLITE_INTEGER)
            ch.bass_note(sqlite3_column_int(raw, 4));
        else
            assert(sqlite3_column_type(raw, 4) == SQLITE_NULL);
        if (sqlite3_column_type(raw, 5) == SQLITE_INTEGER)
            ch.bass_note_step(static_cast<model::chord::flat_sharp>(sqlite3_column_int(raw, 5)));
        else
            assert(sqlite3_column_type(raw, 5) == SQLITE_NULL);
        ch.extensions(reinterpret_cast<const char*>(sqlite3_column_text(raw, 6)))
          .is_staccato(sqlite3_column_int(raw, 7))
          .is_diamond(sqlite3_column_int(raw, 8));
        if (sqlite3_column_type(raw, 9) == SQLITE_INTEGER)
            ch.duration(static_cast<model::chord::time>(sqlite3_column_int(raw, 9)));
        else
            assert(sqlite3_column_type(raw, 9) == SQLITE_NULL);
        ch.is_tied(sqlite3_column_int(raw, 10))
          .is_pushed(sqlite3_column_int(raw, 11));
        chords.push_back(ch);
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error(std::string("Could not retrieve chords: ") + sqlite3_errstr(rc));
    return chords;
}

std::vector<model::song> database::select_songs()
{
    std::vector<model::song> songs;
    assert(prepared_statements_.count(statement::SELECT_SONGS) == 1);
    assert(prepared_statements_.count(statement::SELECT_TIME_SIGNATURE_BY_ID) == 1);
    auto sel_s = prepared_statements_[statement::SELECT_SONGS];
    auto sel_ts = prepared_statements_[statement::SELECT_TIME_SIGNATURE_BY_ID];
    sel_s->reset();
    auto raw = sel_s->ptr();
    auto rc = sqlite3_step(raw);
    unsigned cur_index = 0;
    while (rc == SQLITE_ROW)
    {
        auto song_id = sqlite3_column_int64(raw, 0);
        model::song cur;
        cur.name(reinterpret_cast<const char*>(sqlite3_column_text(raw, 1)));
        cur.key(reinterpret_cast<const char*>(sqlite3_column_text(raw, 2)));
        sel_ts->reset();
        sqlite3_bind_int64(sel_ts->ptr(), 1, sqlite3_column_int64(raw, 3));
        auto rc2 = sqlite3_step(sel_ts->ptr());
        if (rc2 != SQLITE_ROW)
            throw std::runtime_error(std::string("Could not find a time signature:") + sqlite3_errstr(rc2));
        model::time_signature ts;
        ts.kind(static_cast<model::time_signature::beat_type>(sqlite3_column_int(sel_ts->ptr(), 1)))
          .count(sqlite3_column_int(sel_ts->ptr(), 2));
        cur.time_sig(ts);
        cur.bars_per_line(sqlite3_column_int(raw, 4));
        cur.tempo(std::make_tuple(sqlite3_column_int(raw, 5), static_cast<model::chord::time>(sqlite3_column_int(raw, 6))));
        assert(sqlite3_column_int(raw, 7) == cur_index++);
        cur.bars(select_bars(song_id));
        songs.push_back(cur);
        rc = sqlite3_step(raw);
    }
    if (rc != SQLITE_DONE)
        throw std::runtime_error(std::string("Could not retrieve songs: ") + sqlite3_errstr(rc));
    return songs;
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
        throw std::runtime_error(std::string("Could not insert a time signature: ") + sqlite3_errstr(rc));
    assert(sqlite3_column_count(raw) == 1);
    return sqlite3_column_int64(raw, 0);
}

}
