#include <gtest/gtest.h>
#include "../database.hpp"
#include <filesystem>
#include <chrono>

using namespace nashville;
using namespace std::string_literals;

namespace
{

class db_test : public ::testing::Test, public loggable
{
public:
    db_test()
        : loggable("db_test")
    {
    }

protected:
    std::vector<model::playlist> create_playlists(unsigned num)
    {
        std::vector<model::playlist> pls;
        for (int i = 0; i < num; i++)
            pls.emplace_back(std::string("playlist ") + std::to_string(i));
        return pls;
    }

    std::vector<model::song> create_songs(unsigned num)
    {
        std::vector<std::string> keys = { "A", "B", "C", "D", "E", "F", "G",
                                          "A min", "B min", "C min", "D min", "E min", "F min", "G min" };
        std::vector<model::song> songs;
        for (int i = 0; i < num; i++)
        {
            auto s = model::song(std::string("song ") + std::to_string(i));
            s.key(keys[i % 14]);
            model::time_signature ts;
            ts.count((i % 12) + 1);
            switch (i %3)
            {
            case 0:
                ts.kind(model::time_signature::beat_type::EIGHTH);
                break;
            case 1:
                ts.kind(model::time_signature::beat_type::QUARTER);
                break;
            case 2:
                ts.kind(model::time_signature::beat_type::HALF);
                break;
            }
            s.time_sig(ts);
            std::tuple<unsigned, model::chord::time> temp;
            std::get<0>(temp) = i;
            switch (i % 8)
            {
            case 0:
                std::get<1>(temp) = model::chord::time::SIXTEENTH;
                break;
            case 1:
                std::get<1>(temp) = model::chord::time::EIGHTH;
                break;
            case 2:
                std::get<1>(temp) = model::chord::time::DOTTED_EIGHTH;
                break;
            case 3:
                std::get<1>(temp) = model::chord::time::QUARTER;
                break;
            case 4:
                std::get<1>(temp) = model::chord::time::DOTTED_QUARTER;
                break;
            case 5:
                std::get<1>(temp) = model::chord::time::HALF;
                break;
            case 6:
                std::get<1>(temp) = model::chord::time::DOTTED_HALF;
                break;
            case 7:
                std::get<1>(temp) = model::chord::time::WHOLE;
                break;
            }
            s.tempo(temp);
            s.bars_per_line((i % 16) + 1);
            for (int j = 0; j < 35; j++)
            {
                auto& b = s.add_bar();
                b.add_chord().number(7);
                b.add_chord().number(3);
            }
            auto istr = std::to_string(i);
            for (int j = 0; j < i % 7; j++)
                s.meta().authors.push_back("Author "s + std::to_string(j));
            auto is = std::to_string(i);
            s.meta().original_performer = "Performer "s + is;
            s.meta().original_album = "Album "s + is;
            s.meta().notes = "Notes "s + is;
            if (i & 1)
                s.meta().original_album_release_date = std::chrono::time_point_cast<std::chrono::days>(std::chrono::system_clock::now());
            if ((i % 10) != 0)
                s.margin_width((i % 10) * 15);
            songs.push_back(s);
        }
        lgr()->info("Created {} songs", songs.size());
        return songs;
    }

    void expect_bar(const model::bar& lhs, const model::bar& rhs)
    {
        ASSERT_EQ(lhs.chords().size(), rhs.chords().size());
        for (auto i = 0; i < lhs.chords().size(); i++)
            expect_chord(lhs.chords()[i], rhs.chords()[i]);
        EXPECT_EQ(lhs.time_sig(), rhs.time_sig());
        EXPECT_EQ(lhs.is_eol(), rhs.is_eol());
        EXPECT_EQ(lhs.section(), rhs.section());
        EXPECT_EQ(lhs.repeat(), rhs.repeat());
        EXPECT_EQ(lhs.voltas(), rhs.voltas());
    }

    void expect_chord(const model::chord& lhs, const model::chord& rhs)
    {
        EXPECT_EQ(lhs.bass_note(), rhs.bass_note());
        EXPECT_EQ(lhs.bass_note_step(), rhs.bass_note_step());
        EXPECT_EQ(lhs.duration(), rhs.duration());
        EXPECT_EQ(lhs.extensions(), rhs.extensions());
        EXPECT_EQ(lhs.is_diamond(), rhs.is_diamond());
        EXPECT_EQ(lhs.is_pushed(), rhs.is_pushed());
        EXPECT_EQ(lhs.is_staccato(), rhs.is_staccato());
        EXPECT_EQ(lhs.is_tied(), rhs.is_tied());
        EXPECT_EQ(lhs.mode(), rhs.mode());
        EXPECT_EQ(lhs.number(), rhs.number());
        EXPECT_EQ(lhs.step(), rhs.step());
    }

    void expect_playlist(const model::playlist& lhs, const model::playlist& rhs)
    {
        EXPECT_EQ(lhs.name(), rhs.name());
        ASSERT_EQ(lhs.songs().size(), rhs.songs().size());
        for (auto i = 0; i < lhs.songs().size(); i++)
            EXPECT_EQ(lhs.songs()[i], rhs.songs()[i]);
    }

    void expect_annotations(const model::annotations& lhs, const model::annotations& rhs)
    {
        ASSERT_EQ(lhs.text_boxes().size(), rhs.text_boxes().size());
        for (std::size_t i = 0; i < lhs.text_boxes().size(); i++)
        {
            const auto& l = lhs.text_boxes()[i];
            const auto& r = rhs.text_boxes()[i];
            EXPECT_EQ(l.rect, r.rect);
            EXPECT_EQ(l.text, r.text);
        }
        ASSERT_EQ(lhs.connectors().size(), rhs.connectors().size());
        for (std::size_t i = 0; i < lhs.connectors().size(); i++)
        {
            const auto& l = lhs.connectors()[i];
            const auto& r = rhs.connectors()[i];
            EXPECT_EQ(l.arrow_at_start, r.arrow_at_start);
            EXPECT_EQ(l.arrow_at_end,   r.arrow_at_end);
            ASSERT_EQ(l.start.k, r.start.k);
            ASSERT_EQ(l.end.k,   r.end.k);
            if (l.start.k == model::connector_endpoint::kind::free)
                EXPECT_EQ(l.start.free_pos, r.start.free_pos);
            else
            {
                // text_box_id is session-local; verify the endpoint references
                // the same positional slot in the text_box list instead.
                EXPECT_EQ(l.start.anchor_index, r.start.anchor_index);
                auto lhs_slot = std::find_if(lhs.text_boxes().begin(), lhs.text_boxes().end(),
                    [&](const model::text_box& tb){ return tb.id == l.start.text_box_id; })
                    - lhs.text_boxes().begin();
                auto rhs_slot = std::find_if(rhs.text_boxes().begin(), rhs.text_boxes().end(),
                    [&](const model::text_box& tb){ return tb.id == r.start.text_box_id; })
                    - rhs.text_boxes().begin();
                EXPECT_EQ(lhs_slot, rhs_slot);
            }
            if (l.end.k == model::connector_endpoint::kind::free)
                EXPECT_EQ(l.end.free_pos, r.end.free_pos);
            else
            {
                EXPECT_EQ(l.end.anchor_index, r.end.anchor_index);
                auto lhs_slot = std::find_if(lhs.text_boxes().begin(), lhs.text_boxes().end(),
                    [&](const model::text_box& tb){ return tb.id == l.end.text_box_id; })
                    - lhs.text_boxes().begin();
                auto rhs_slot = std::find_if(rhs.text_boxes().begin(), rhs.text_boxes().end(),
                    [&](const model::text_box& tb){ return tb.id == r.end.text_box_id; })
                    - rhs.text_boxes().begin();
                EXPECT_EQ(lhs_slot, rhs_slot);
            }
        }
    }

    void expect_song(const model::song& lhs, const model::song& rhs)
    {
        ASSERT_EQ(lhs.bars().size(), rhs.bars().size());
        for (auto i = 0; i < lhs.bars().size(); i++)
            expect_bar(lhs.bars()[i], rhs.bars()[i]);
        EXPECT_EQ(lhs.name(), rhs.name());
        EXPECT_EQ(lhs.key(), rhs.key());
        EXPECT_EQ(lhs.time_sig(), rhs.time_sig());
        EXPECT_EQ(lhs.tempo(), rhs.tempo());
        EXPECT_EQ(lhs.bars_per_line(), rhs.bars_per_line());
        EXPECT_EQ(lhs.meta().creation_time, rhs.meta().creation_time);
        EXPECT_EQ(lhs.meta().modification_time, rhs.meta().modification_time);
        EXPECT_EQ(lhs.meta().authors, rhs.meta().authors);
        EXPECT_EQ(lhs.meta().original_performer, rhs.meta().original_performer);
        EXPECT_EQ(lhs.meta().original_album, rhs.meta().original_album);
        EXPECT_EQ(lhs.meta().notes, rhs.meta().notes);
        EXPECT_EQ(lhs.meta().original_album_release_date, rhs.meta().original_album_release_date);
        EXPECT_EQ(lhs.margin_width(), rhs.margin_width());
        expect_annotations(lhs.annotes(), rhs.annotes());
    }

    virtual void SetUp() override
    {
        std::filesystem::path fname = ":memory:";
        auto dir = std::getenv("DB_DIR");
        if (dir != nullptr)
        {
            std::filesystem::path fname = dir;
            std::filesystem::create_directories(fname);
            fname /= std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".nashv";
        }
        try
        {
            db_.reset(new database(fname));
        }
        catch (std::exception& e)
        {
            std::cout << "The database '" << fname << "' could not be opened: " << e.what() << std::endl;
            std::abort();
        }
    }

    virtual void TearDown() override
    {
        db_.reset();
    }

    std::unique_ptr<database> db_;
};

}

TEST_F(db_test, one_song)
{
    model::song s("doggies");
    s.add_bar().add_chord().number(1).mode(model::chord::type::MAJOR);
    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    auto start = std::chrono::high_resolution_clock::now();
    EXPECT_NO_THROW(found = db_->select_song("doggies"));
    std::chrono::duration<double, std::micro> elapsed = std::chrono::high_resolution_clock::now() - start;
    lgr()->info("Selecting one song took {} microseconds", elapsed.count());
    lgr()->info("About to compare simple song");
    expect_song(s, found);
    EXPECT_NO_THROW(db_->remove_song("doggies"));
    EXPECT_THROW(db_->select_song("doggies"), std::runtime_error);
}

TEST_F(db_test, all_chord_attrs)
{
    model::song s("funny chord");
    auto& b = s.add_bar();
    auto& ch = b.add_chord();
    ch.number(7)
      .bass_note(4)
      .bass_note_step(model::chord::flat_sharp::FLAT)
      .duration(model::chord::time::DOTTED_HALF)
      .extensions("maj7")
      .is_diamond(true)
      .is_pushed(true)
      .is_tied(true)
      .mode(model::chord::type::AUGMENTED)
      .step(model::chord::flat_sharp::FLAT);
    auto& ch2 = b.add_chord();
    ch2.number(2)
       .bass_note(3)
       .bass_note_step(model::chord::flat_sharp::SHARP)
       .duration(model::chord::time::EIGHTH)
       .extensions("add6")
       .is_staccato(true)
       .mode(model::chord::type::DIMINISHED);
    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("funny chord"));
    lgr()->info("About to compare funny chord");
    expect_song(s, found);
}

TEST_F(db_test, all_bar_attrs)
{
    model::song s("bar attrs");
    auto& b = s.add_bar();
    b.is_eol(true)
     .section("doggies")
     .time_sig(model::time_signature().count(8).kind(model::time_signature::beat_type::EIGHTH))
     .repeat(model::bar::repeat_status::END)
     .add_volta(0)
     .add_chord();
    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("bar attrs"));
    lgr()->info("About to compare funny bar");
    expect_song(s, found);
}

TEST_F(db_test, all_song_attrs)
{
    model::song s("song attrs");
    s.key("G Minor")
     .time_sig(model::time_signature().count(12).kind(model::time_signature::beat_type::EIGHTH))
     .tempo({ 240, model::chord::time::QUARTER })
     .bars_per_line(72);
    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("song attrs"));
    lgr()->info("About to compare song attrs");
    expect_song(s, found);
}

TEST_F(db_test, lots_of_bars)
{
    model::song s("lots of bars");
    s.time_sig(model::time_signature().count(4).kind(model::time_signature::beat_type::QUARTER))
     .key("C");
    model::time_signature ts;
    for (int i = 0; i < 10000; i++)
    {
        auto& b = s.add_bar();
        if (i % 4 != 0)
        {
            switch (i % 3)
            {
            case 0:
                ts.kind(model::time_signature::beat_type::EIGHTH);
                break;
            case 1:
                ts.kind(model::time_signature::beat_type::QUARTER);
                break;
            case 2:
                ts.kind(model::time_signature::beat_type::HALF);
                break;
            }
            ts.count(i % 8);
            b.time_sig(ts);
        }
        b.is_eol(i & 1)
         .section(std::to_string(i) + " section");
        auto& ch = b.add_chord();
        ch.number((i % 7) + 1);
    }
    auto start = std::chrono::high_resolution_clock::now();
    EXPECT_NO_THROW(db_->insert_song(s));
    std::chrono::duration<double, std::milli> elapsed = std::chrono::high_resolution_clock::now() - start;
    lgr()->info("Inserting one song took {} milliseconds", elapsed.count());
    model::song found;
    start = std::chrono::high_resolution_clock::now();
    EXPECT_NO_THROW(found = db_->select_song("lots of bars"));
    elapsed = std::chrono::high_resolution_clock::now() - start;
    lgr()->info("Selecting one song took {} milliseconds", elapsed.count());
    lgr()->info("About to compare lots of bars");
    expect_song(s, found);
}

TEST_F(db_test, lots_of_chords)
{
    model::song s("lots of chords");
    s.time_sig(model::time_signature().count(8).kind(model::time_signature::beat_type::EIGHTH))
     .key("D minor");
    auto& b = s.add_bar();
    for (int i = 0; i < 100; i++)
    {
        for (int j = 1; j <= 7; j++)
        {
            for (int k = 0; k <= 7; k++)
            {
                auto& ch = b.add_chord();
                ch.number(j);
                if (k > 0)
                    ch.bass_note(k);
                switch (k % 3)
                {
                case 1:
                    ch.bass_note_step(model::chord::flat_sharp::FLAT);
                    break;
                case 2:
                    ch.bass_note_step(model::chord::flat_sharp::FLAT);
                    break;
                }
                switch (i % 9)
                {
                case 1:
                    ch.duration(model::chord::time::SIXTEENTH);
                    break;
                case 2:
                    ch.duration(model::chord::time::EIGHTH);
                    break;
                case 3:
                    ch.duration(model::chord::time::DOTTED_EIGHTH);
                    break;
                case 4:
                    ch.duration(model::chord::time::QUARTER);
                    break;
                case 5:
                    ch.duration(model::chord::time::DOTTED_QUARTER);
                    break;
                case 6:
                    ch.duration(model::chord::time::HALF);
                    break;
                case 7:
                    ch.duration(model::chord::time::DOTTED_HALF);
                    break;
                case 8:
                    ch.duration(model::chord::time::WHOLE);
                    break;
                }
                ch.extensions(std::string("ext ") + std::to_string(i & 1));
                ch.is_diamond(i & 1);
                ch.is_pushed(i & 1);
                ch.is_tied(i % 1);
                ch.mode(static_cast<model::chord::type>(i % 5));
            }
        }
    }
    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("lots of chords"));
    lgr()->info("About to compare lots of chords");
    expect_song(s, found);
}

TEST_F(db_test, lots_of_songs)
{
    auto songs = create_songs(10000);
    lgr()->info("Inserting {} songs", songs.size());
    for (const auto& s : songs)
        EXPECT_NO_THROW(db_->insert_song(s));
    std::vector<model::song> found;
    lgr()->info("Retrieving {} songs", songs.size());
    for (const auto& s : songs)
        EXPECT_NO_THROW(found.push_back(db_->select_song(s.name())));
    ASSERT_EQ(songs.size(), found.size());
    lgr()->info("About to compare lots of songs");
    for (int i = 0; i < songs.size(); i++)
        expect_song(songs[i], found[i]);
    auto start = std::chrono::high_resolution_clock::now();
    EXPECT_NO_THROW(db_->select_song(songs[4000].name()));
    std::chrono::duration<double, std::micro> elapsed = std::chrono::high_resolution_clock::now() - start;
    lgr()->info("Selecting one song took {} microseconds", elapsed.count());
    start = std::chrono::high_resolution_clock::now();
    EXPECT_NO_THROW(db_->remove_song(songs[7000].name()));
    elapsed = std::chrono::high_resolution_clock::now() - start;
    lgr()->info("Removing one song took {} microseconds", elapsed.count());
    EXPECT_THROW(db_->select_song(songs[7000].name()), std::runtime_error);
}

TEST_F(db_test, lots_of_song_names)
{
    auto songs = create_songs(10000);
    lgr()->info("Inserting {} songs", songs.size());
    for (const auto& s : songs)
        EXPECT_NO_THROW(db_->insert_song(s));
    std::vector<std::string> names;
    EXPECT_NO_THROW(names = db_->select_song_names());
    std::vector<std::string> org_names;
    for (const auto& s : songs)
        org_names.push_back(s.name());
    std::sort(org_names.begin(), org_names.end());
    std::sort(names.begin(), names.end());
    EXPECT_EQ(org_names, names);
}

TEST_F(db_test, one_playlist)
{
    auto pls = create_playlists(1);
    auto songs = create_songs(10);
    for (const auto& s : songs)
    {
        EXPECT_NO_THROW(db_->insert_song(s));
        pls[0].add_song(s.name());
    }
    EXPECT_NO_THROW(db_->insert_playlist(pls[0]));
    EXPECT_NO_THROW(db_->remove_playlist(pls[0].name()));
    std::vector<std::string> names;
    EXPECT_NO_THROW(names = db_->select_song_names());
    std::vector<std::string> org_names;
    for (const auto& s : songs)
        org_names.push_back(s.name());
    std::sort(org_names.begin(), org_names.end());
    std::sort(names.begin(), names.end());
}

TEST_F(db_test, move_to_file)
{
    auto songs = create_songs(1000);
    database mem;
    for (const auto& s : songs)
        EXPECT_NO_THROW(mem.insert_song(s));
    auto playlists = create_playlists(100);
    for (int i = 0; i < playlists.size(); i++)
        playlists[i].add_song(songs[i % 10].name());
    for (const auto& pl : playlists)
        EXPECT_NO_THROW(mem.insert_playlist(pl));
    std::filesystem::path fname("./move_to_file.nashv");
    std::filesystem::remove(fname);
    ASSERT_TRUE(mem.in_memory());
    EXPECT_TRUE(mem.file_name().empty());
    EXPECT_NO_THROW(mem.move_to_file(fname));
    for (const auto& s : songs)
    {
        model::song found_s;
        EXPECT_NO_THROW(found_s = mem.select_song(s.name()));
        expect_song(s, found_s);
    }
    for (const auto& p : playlists)
    {
        model::playlist found_p("uh");
        EXPECT_NO_THROW(found_p = mem.select_playlist(p.name()));
        expect_playlist(p, found_p);
    }
    ASSERT_FALSE(mem.in_memory());
    fname = std::filesystem::absolute(fname).lexically_normal();
    EXPECT_EQ(fname, mem.file_name());
    std::filesystem::remove(fname);
}

TEST_F(db_test, lots_of_playlists)
{
    auto songs = create_songs(10000);
    lgr()->info("Inserting {} songs", songs.size());
    for (const auto& s : songs)
        EXPECT_NO_THROW(db_->insert_song(s));
    std::vector<model::playlist> playlists;
    for (unsigned i = 0; i < songs.size(); i += 10)
    {
        auto cur = model::playlist(std::string("playlist ") + std::to_string(i));
        for (unsigned j = 0; j < 10; j++)
            cur.add_song(songs[j * 10].name());
        playlists.push_back(cur);
    }
    lgr()->info("Inserting {} playlists", playlists.size());
    for (const auto& p : playlists)
        EXPECT_NO_THROW(db_->insert_playlist(p));
    lgr()->info("Retrieving {} playlists", playlists.size());
    std::vector<std::string> found;
    EXPECT_NO_THROW(found = db_->select_playlist_names());
    ASSERT_EQ(playlists.size(), found.size());
    for (const auto& p : playlists)
    {
        model::playlist sel("uh");
        EXPECT_NO_THROW(sel = db_->select_playlist(p.name()));
        ASSERT_EQ(p.songs().size(), sel.songs().size());
        for (unsigned j = 0; j < p.songs().size(); j++)
            EXPECT_EQ(p.songs()[j], sel.songs()[j]);
    }
}

// ---------------------------------------------------------------------------
// rename_song
// ---------------------------------------------------------------------------

TEST_F(db_test, rename_song_basic)
{
    model::song s("original name");
    s.add_bar().add_chord().number(1);
    EXPECT_NO_THROW(db_->insert_song(s));

    EXPECT_NO_THROW(db_->rename_song("original name", "new name"));

    // old name is gone
    EXPECT_THROW(db_->select_song("original name"), std::runtime_error);

    // new name returns the same song content
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("new name"));
    s.name("new name");
    expect_song(s, found);
}

TEST_F(db_test, rename_song_preserves_annotations)
{
    model::song s("annotated song");
    s.add_bar().add_chord().number(3);
    const auto tb_id = s.annotes().add_text_box(QRectF(10, 20, 100, 40), "hello").id;
    s.annotes().add_connector(QPointF(5, 5), QPointF(50, 50));
    s.annotes().add_connector(
        model::connector_endpoint::make_anchor(tb_id, 2),
        model::connector_endpoint::make_free(QPointF(200, 200)));
    EXPECT_NO_THROW(db_->insert_song(s));

    EXPECT_NO_THROW(db_->rename_song("annotated song", "renamed annotated"));

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("renamed annotated"));
    s.name("renamed annotated");
    expect_song(s, found);
}

TEST_F(db_test, rename_song_appears_in_song_names)
{
    auto songs = create_songs(5);
    for (const auto& s : songs)
        EXPECT_NO_THROW(db_->insert_song(s));

    EXPECT_NO_THROW(db_->rename_song(songs[2].name(), "completely different"));

    auto names = db_->select_song_names();
    EXPECT_EQ(std::count(names.begin(), names.end(), songs[2].name()), 0);
    EXPECT_EQ(std::count(names.begin(), names.end(), "completely different"), 1);
    EXPECT_EQ(names.size(), songs.size());
}

TEST_F(db_test, rename_song_nonexistent_throws)
{
    EXPECT_THROW(db_->rename_song("does not exist", "whatever"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// rename_playlist
// ---------------------------------------------------------------------------

TEST_F(db_test, rename_playlist_basic)
{
    auto songs = create_songs(3);
    for (const auto& s : songs)
        EXPECT_NO_THROW(db_->insert_song(s));

    model::playlist pl("old playlist");
    for (const auto& s : songs)
        pl.add_song(s.name());
    EXPECT_NO_THROW(db_->insert_playlist(pl));

    EXPECT_NO_THROW(db_->rename_playlist("old playlist", "new playlist"));

    // old name is gone — select_playlist returns an empty playlist for unknown names,
    // so verify via the names list rather than expecting a throw
    std::vector<std::string> pl_names;
    EXPECT_NO_THROW(pl_names = db_->select_playlist_names());
    EXPECT_EQ(std::count(pl_names.begin(), pl_names.end(), "old playlist"), 0);
    EXPECT_EQ(std::count(pl_names.begin(), pl_names.end(), "new playlist"), 1);

    // new name returns the same content
    model::playlist found("x");
    EXPECT_NO_THROW(found = db_->select_playlist("new playlist"));
    pl.name("new playlist");
    expect_playlist(pl, found);
}

TEST_F(db_test, rename_playlist_appears_in_playlist_names)
{
    auto songs = create_songs(2);
    for (const auto& s : songs)
        EXPECT_NO_THROW(db_->insert_song(s));

    for (int i = 0; i < 4; i++)
    {
        model::playlist pl("playlist "s + std::to_string(i));
        for (const auto& s : songs)
            pl.add_song(s.name());
        EXPECT_NO_THROW(db_->insert_playlist(pl));
    }

    EXPECT_NO_THROW(db_->rename_playlist("playlist 1", "renamed playlist"));

    auto names = db_->select_playlist_names();
    EXPECT_EQ(std::count(names.begin(), names.end(), "playlist 1"), 0);
    EXPECT_EQ(std::count(names.begin(), names.end(), "renamed playlist"), 1);
    EXPECT_EQ(names.size(), 4u);
}

TEST_F(db_test, rename_playlist_nonexistent_throws)
{
    EXPECT_THROW(db_->rename_playlist("ghost playlist", "whatever"), std::runtime_error);
}

// ---------------------------------------------------------------------------
// insert_song with annotations (fresh insert)
// ---------------------------------------------------------------------------

TEST_F(db_test, insert_song_text_boxes_only)
{
    model::song s("tb song");
    s.add_bar().add_chord().number(1);
    s.annotes().add_text_box(QRectF(0,   0,  80, 30), "first box");
    s.annotes().add_text_box(QRectF(100, 50, 60, 20), "second box");
    s.annotes().add_text_box(QRectF(200, 10, 40, 40), "");   // empty text is valid

    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("tb song"));
    expect_song(s, found);
}

TEST_F(db_test, insert_song_free_connectors)
{
    model::song s("free conn song");
    s.add_bar().add_chord().number(2);
    auto& c1 = s.annotes().add_connector(QPointF(0, 0), QPointF(100, 100));
    c1.arrow_at_start = true;
    c1.arrow_at_end   = false;
    auto& c2 = s.annotes().add_connector(QPointF(50, 50), QPointF(250, 300));
    c2.arrow_at_start = false;
    c2.arrow_at_end   = true;
    auto& c3 = s.annotes().add_connector(QPointF(10, 90), QPointF(10, 10));
    c3.arrow_at_start = true;
    c3.arrow_at_end   = true;

    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("free conn song"));
    expect_song(s, found);
}

TEST_F(db_test, insert_song_anchored_connectors)
{
    model::song s("anchored song");
    s.add_bar().add_chord().number(5);
    // Save ids by value before adding more text boxes — add_text_box returns a
    // reference into a std::vector; a subsequent push_back may reallocate,
    // making any held reference dangle.
    const auto tb0_id = s.annotes().add_text_box(QRectF(10,  10, 80, 30), "box A").id;
    const auto tb1_id = s.annotes().add_text_box(QRectF(200, 10, 80, 30), "box B").id;

    // anchor-to-anchor
    s.annotes().add_connector(
        model::connector_endpoint::make_anchor(tb0_id, 3),
        model::connector_endpoint::make_anchor(tb1_id, 7));

    // anchor-to-free
    auto& c2 = s.annotes().add_connector(
        model::connector_endpoint::make_anchor(tb0_id, 1),
        model::connector_endpoint::make_free(QPointF(500, 500)));
    c2.arrow_at_start = true;

    // free-to-anchor
    s.annotes().add_connector(
        model::connector_endpoint::make_free(QPointF(0, 0)),
        model::connector_endpoint::make_anchor(tb1_id, 5));

    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("anchored song"));
    expect_song(s, found);
}

TEST_F(db_test, insert_song_no_annotations)
{
    // Verify that a song with no annotations round-trips cleanly and
    // that empty annotation tables stay empty after the insert.
    model::song s("bare song");
    s.add_bar().add_chord().number(1);
    ASSERT_TRUE(s.annotes().empty());

    EXPECT_NO_THROW(db_->insert_song(s));
    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("bare song"));
    expect_song(s, found);
    EXPECT_TRUE(found.annotes().empty());
}

// ---------------------------------------------------------------------------
// update_song (UPSERT) — song table columns, with and without annotations
// ---------------------------------------------------------------------------

TEST_F(db_test, update_song_all_columns_no_annotations)
{
    // Insert a song, then re-insert it with every column changed and
    // verify the select returns the updated values.
    model::song s("mutable song");
    s.key("C")
     .time_sig(model::time_signature().count(4).kind(model::time_signature::beat_type::QUARTER))
     .tempo({ 120, model::chord::time::QUARTER })
     .bars_per_line(4);
    s.meta().authors             = { "Author A" };
    s.meta().original_performer  = "Performer A";
    s.meta().original_album      = "Album A";
    s.meta().notes               = "Notes A";
    s.meta().original_album_release_date = std::nullopt;
    s.add_bar().add_chord().number(1);

    EXPECT_NO_THROW(db_->insert_song(s));

    // Mutate every column
    s.key("F# minor")
     .time_sig(model::time_signature().count(12).kind(model::time_signature::beat_type::EIGHTH))
     .tempo({ 240, model::chord::time::HALF })
     .bars_per_line(8);
    s.meta().authors             = { "Author B", "Author C", "Author D" };
    s.meta().original_performer  = "Performer B";
    s.meta().original_album      = "Album B";
    s.meta().notes               = "Notes B — unicode: \xc3\xa9\xc3\xa0\xc3\xbc";
    s.meta().original_album_release_date =
        std::chrono::time_point_cast<std::chrono::days>(std::chrono::system_clock::now());
    s.meta().modification_time =
        std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());

    // Replace bars too
    s.bars({});
    for (int i = 0; i < 8; i++)
        s.add_bar().add_chord().number((i % 7) + 1);

    EXPECT_NO_THROW(db_->insert_song(s));   // UPSERT

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("mutable song"));
    expect_song(s, found);
}

TEST_F(db_test, update_song_clears_empty_authors)
{
    // Authors is multi-valued; verify going from some authors to none persists.
    model::song s("author clearance");
    s.meta().authors = { "X", "Y", "Z" };
    s.add_bar().add_chord().number(1);
    EXPECT_NO_THROW(db_->insert_song(s));

    s.meta().authors.clear();
    EXPECT_NO_THROW(db_->insert_song(s));

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("author clearance"));
    EXPECT_TRUE(found.meta().authors.empty());
}

TEST_F(db_test, update_song_clears_release_date)
{
    // Verify going from a set release date to nullopt persists.
    model::song s("date clearance");
    s.meta().original_album_release_date =
        std::chrono::time_point_cast<std::chrono::days>(std::chrono::system_clock::now());
    s.add_bar().add_chord().number(1);
    EXPECT_NO_THROW(db_->insert_song(s));

    s.meta().original_album_release_date = std::nullopt;
    EXPECT_NO_THROW(db_->insert_song(s));

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("date clearance"));
    EXPECT_FALSE(found.meta().original_album_release_date.has_value());
}

TEST_F(db_test, update_song_adds_annotations)
{
    // Start with no annotations, update to have some.
    model::song s("gains annotations");
    s.add_bar().add_chord().number(1);
    EXPECT_NO_THROW(db_->insert_song(s));

    s.annotes().add_text_box(QRectF(5, 5, 50, 20), "added later");
    s.annotes().add_connector(QPointF(0, 0), QPointF(100, 100));
    EXPECT_NO_THROW(db_->insert_song(s));

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("gains annotations"));
    expect_song(s, found);
}

TEST_F(db_test, update_song_removes_annotations)
{
    // Start with annotations, update to have none — old rows must be gone.
    model::song s("loses annotations");
    s.add_bar().add_chord().number(1);
    s.annotes().add_text_box(QRectF(10, 10, 80, 30), "transient");
    s.annotes().add_connector(QPointF(1, 1), QPointF(9, 9));
    EXPECT_NO_THROW(db_->insert_song(s));

    // Replace song with a fresh one that has no annotations (same name)
    model::song s2("loses annotations");
    s2.add_bar().add_chord().number(1);
    ASSERT_TRUE(s2.annotes().empty());
    EXPECT_NO_THROW(db_->insert_song(s2));

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("loses annotations"));
    EXPECT_TRUE(found.annotes().empty());
}

TEST_F(db_test, update_song_replaces_annotations)
{
    // Update a song twice with different annotation sets; only the latest
    // should survive — no accumulation of stale rows.
    model::song s("annotation replacement");
    s.add_bar().add_chord().number(4);

    // First save: two text boxes, one connector
    s.annotes().add_text_box(QRectF(0,  0,  60, 25), "first A");
    s.annotes().add_text_box(QRectF(70, 0,  60, 25), "first B");
    s.annotes().add_connector(QPointF(60, 12), QPointF(70, 12));
    EXPECT_NO_THROW(db_->insert_song(s));

    // Second save: completely different annotation set (same song name = UPSERT)
    model::song s2("annotation replacement");
    s2.add_bar().add_chord().number(4);
    const auto tb_id = s2.annotes().add_text_box(QRectF(200, 200, 90, 35), "second only").id;
    s2.annotes().add_connector(
        model::connector_endpoint::make_anchor(tb_id, 0),
        model::connector_endpoint::make_free(QPointF(10, 10)));
    s2.annotes().add_connector(QPointF(300, 300), QPointF(400, 400));
    EXPECT_NO_THROW(db_->insert_song(s2));

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("annotation replacement"));
    expect_song(s2, found);

    // Counts must match exactly — no ghost rows from first save
    EXPECT_EQ(found.annotes().text_boxes().size(), 1u);
    EXPECT_EQ(found.annotes().connectors().size(), 2u);
}

TEST_F(db_test, update_song_all_columns_with_annotations)
{
    // Combined: change every song-table column AND swap the annotation set
    // in a single UPSERT, then verify the full round-trip.
    model::song s("full update");
    s.key("D")
     .time_sig(model::time_signature().count(4).kind(model::time_signature::beat_type::QUARTER))
     .tempo({ 100, model::chord::time::QUARTER })
     .bars_per_line(4);
    s.meta().authors            = { "Original Author" };
    s.meta().original_performer = "Original Performer";
    s.meta().original_album     = "Original Album";
    s.meta().notes              = "Original notes";
    s.add_bar().add_chord().number(1);
    s.annotes().add_text_box(QRectF(0, 0, 50, 20), "old box");
    s.annotes().add_connector(QPointF(0, 0), QPointF(50, 50));
    EXPECT_NO_THROW(db_->insert_song(s));

    // New version: every column different, different annotation set
    model::song s2("full update");
    s2.key("Bb major")
      .time_sig(model::time_signature().count(6).kind(model::time_signature::beat_type::EIGHTH))
      .tempo({ 180, model::chord::time::DOTTED_QUARTER })
      .bars_per_line(3);
    s2.meta().authors            = { "New A", "New B" };
    s2.meta().original_performer = "New Performer";
    s2.meta().original_album     = "New Album";
    s2.meta().notes              = "New notes";
    s2.meta().original_album_release_date =
        std::chrono::time_point_cast<std::chrono::days>(std::chrono::system_clock::now());
    s2.meta().modification_time =
        std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    for (int i = 0; i < 6; i++)
        s2.add_bar().add_chord().number((i % 7) + 1);
    const auto tb0_id = s2.annotes().add_text_box(QRectF(10,  10, 80, 30), "new box X").id;
    const auto tb1_id = s2.annotes().add_text_box(QRectF(200, 10, 80, 30), "new box Y").id;
    s2.annotes().add_connector(
        model::connector_endpoint::make_anchor(tb0_id, 3),
        model::connector_endpoint::make_anchor(tb1_id, 7));
    s2.annotes().add_connector(QPointF(99, 99), QPointF(1, 1));

    EXPECT_NO_THROW(db_->insert_song(s2));

    model::song found;
    EXPECT_NO_THROW(found = db_->select_song("full update"));
    expect_song(s2, found);
}
