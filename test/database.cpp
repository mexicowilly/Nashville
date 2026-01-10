#include <gtest/gtest.h>
#include "../database.hpp"
#include <filesystem>
#include <chucho/log.hpp>

using namespace nashville;

namespace
{

class db_test : public ::testing::Test, public chucho::loggable<db_test>
{
protected:
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
        std::string fname = ":memory:";
        auto dir = std::getenv("DB_DIR");
        if (dir != nullptr)
        {
            std::filesystem::path p(dir);
            std::filesystem::create_directories(p);
            p /= std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".nashv";
            fname = p.string();
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
    EXPECT_NO_THROW(db_->insert_songs({ s }));
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    ASSERT_EQ(1, found.size());
    CHUCHO_INFO_L("About to compare simple song");
    expect_song(s, found[0]);
}

TEST_F(db_test, all_chord_attrs)
{
    model::song s("funny chord");
    auto& ch = s.add_bar().add_chord();
    ch.number(7)
      .bass_note(4)
      .bass_note_step(model::chord::flat_sharp::FLAT)
      .duration(model::chord::time::DOTTED_HALF)
      .extensions("maj7")
      .is_diamond(true)
      .is_pushed(true)
      .is_staccato(true)
      .is_tied(true)
      .mode(model::chord::type::AUGMENTED)
      .step(model::chord::flat_sharp::FLAT);
    EXPECT_NO_THROW(db_->insert_songs({ s }));
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    ASSERT_EQ(1, found.size());
    CHUCHO_INFO_L("About to compare funny chord");
    expect_song(s, found[0]);
}

TEST_F(db_test, all_bar_attrs)
{
    model::song s("bar attrs");
    auto& b = s.add_bar();
    b.is_eol(true)
     .section("doggies")
     .time_sig(model::time_signature().count(8).kind(model::time_signature::beat_type::EIGHTH))
     .add_chord();
    EXPECT_NO_THROW(db_->insert_songs({ s }));
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    ASSERT_EQ(1, found.size());
    CHUCHO_INFO_L("About to compare funny bar");
    expect_song(s, found[0]);
}

TEST_F(db_test, all_song_attrs)
{
    model::song s("song attrs");
    s.key("G Minor")
     .time_sig(model::time_signature().count(12).kind(model::time_signature::beat_type::EIGHTH))
     .tempo({ 240, model::chord::time::QUARTER })
     .bars_per_line(72);
    EXPECT_NO_THROW(db_->insert_songs({ s }));
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    ASSERT_EQ(1, found.size());
    CHUCHO_INFO_L("About to compare song attrs");
    expect_song(s, found[0]);
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
    EXPECT_NO_THROW(db_->insert_songs({ s }));
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    ASSERT_EQ(1, found.size());
    CHUCHO_INFO_L("About to compare lots of bars");
    expect_song(s, found[0]);
}

TEST_F(db_test, lots_of_chords)
{
    model::song s("lots of chords");
    s.time_sig(model::time_signature().count(8).kind(model::time_signature::beat_type::EIGHTH))
     .key("D minor");
    auto& b = s.add_bar();
    for (int i = 0; i < 5000; i++)
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
                ch.is_staccato(i & 1);
                ch.is_tied(i % 1);
                ch.mode(static_cast<model::chord::type>(i % 5));
            }
        }
    }
    EXPECT_NO_THROW(db_->insert_songs({ s }));
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    ASSERT_EQ(1, found.size());
    CHUCHO_INFO_L("About to compare lots of chords");
    expect_song(s, found[0]);
}

TEST_F(db_test, lots_of_songs)
{
    std::vector<std::string> keys = { "A", "B", "C", "D", "E", "F", "G",
                                      "A min", "B min", "C min", "D min", "E min", "F min", "G min" };
    std::vector<model::song> songs;
    for (int i = 0; i < 500; i++)
    {
        auto s = model::song(std::string("name ") + std::to_string(i));
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
        s.bars_per_line((i% 16) + 1);
        for (int j = 0; j < 35; j++)
        {
            auto& b = s.add_bar();
            b.add_chord().number(7);
            b.add_chord().number(3);
        }
        songs.push_back(s);
    }
    EXPECT_NO_THROW(db_->insert_songs(songs));
    CHUCHO_INFO_L("Inserted " << songs.size() << " songs");
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    CHUCHO_INFO_L("Loaded " << songs.size() << " songs");
    ASSERT_EQ(500, found.size());
    CHUCHO_INFO_L("About to compare lots of songs");
    for (int i = 0; i < 500; i++)
        expect_song(songs[i], found[i]);
}
