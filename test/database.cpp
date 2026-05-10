#include <gtest/gtest.h>
#include "../database.hpp"
#include <filesystem>
#include <chrono>

using namespace nashville;

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
