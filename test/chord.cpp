#include <gtest/gtest.h>
#include "../chord.hpp"
#include <array>

using namespace nashville;

TEST(chord, user_input)
{
    std::array<std::string, 7> valid_numbers = { "1", "2", "3", "4", "5", "6", "7" };
    model::chord c;
    model::chord ref;
    for (const auto& num : valid_numbers)
    {
        EXPECT_NO_THROW(c.parse_user_input(num));
        ref.number(num[0] - '0');
        EXPECT_EQ(c, ref);
    }
    EXPECT_THROW(c.parse_user_input("0"), std::invalid_argument);
    EXPECT_THROW(c.parse_user_input("8"), std::invalid_argument);
    EXPECT_THROW(c.parse_user_input("9"), std::invalid_argument);
    c = model::chord();
    EXPECT_NO_THROW(c.parse_user_input("b5"));
    ref.number(5).step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    c = model::chord();
    EXPECT_NO_THROW(c.parse_user_input("#7"));
    ref.number(7).step(model::chord::flat_sharp::SHARP);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("6-"));
    ref = model::chord(6, model::chord::type::MINOR);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("7dim"));
    ref = model::chord(7, model::chord::type::DIMINISHED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("4+"));
    ref = model::chord(4, model::chord::type::AUGMENTED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("b3-7"));
    ref = model::chord()
          .number(3)
          .step(model::chord::flat_sharp::FLAT)
          .mode(model::chord::type::MINOR)
          .extensions("7");
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("#7diddly"));
    ref = model::chord()
          .number(7)
          .step(model::chord::flat_sharp::SHARP)
          .extensions("diddly");
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("6/1"));
    ref = model::chord()
          .number(6)
          .bass_note(1);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("3diddly/b7"));
    ref = model::chord()
          .number(3)
          .extensions("diddly")
          .bass_note(7)
          .bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("3dimdiddly/b7"));
    ref.mode(model::chord::type::DIMINISHED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("#2-/b5"));
    ref = model::chord()
          .number(2)
          .mode(model::chord::type::MINOR)
          .step(model::chord::flat_sharp::SHARP)
          .bass_note(5)
          .bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:s"));
    ref = model::chord().duration(model::chord::time::SIXTEENTH);
    EXPECT_EQ(c, ref);
    // There is no dotted sixteenth, but we allow it and ignore
    EXPECT_NO_THROW(c.parse_user_input("1:s."));
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:e"));
    ref.duration(model::chord::time::EIGHTH);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:e."));
    ref.duration(model::chord::time::DOTTED_EIGHTH);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:q"));
    ref.duration(model::chord::time::QUARTER);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:q."));
    ref.duration(model::chord::time::DOTTED_QUARTER);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:h"));
    ref.duration(model::chord::time::HALF);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:h."));
    ref.duration(model::chord::time::DOTTED_HALF);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:w"));
    ref.duration(model::chord::time::WHOLE);
    EXPECT_EQ(c, ref);
    // There is no dotted whole, but we allow it and ignore
    EXPECT_NO_THROW(c.parse_user_input("1:w."));
    EXPECT_EQ(c, ref);
    EXPECT_THROW(c.parse_user_input("mydoghasfleas"), std::invalid_argument);
    EXPECT_THROW(c.parse_user_input("7dim:blah"), std::invalid_argument);
}
