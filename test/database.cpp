#include <gtest/gtest.h>
#include "../database.hpp"
#include <filesystem>

using namespace nashville;

namespace
{

class db_test : public ::testing::Test
{
protected:
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
    s.add_bar().add_chord().number(1);
    EXPECT_NO_THROW(db_->insert_songs({ s }));
    std::vector<model::song> found;
    EXPECT_NO_THROW(found = db_->select_songs());
    ASSERT_EQ(1, found.size());
}
