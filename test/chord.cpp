#include <gtest/gtest.h>
#include "../model/chord.hpp"

using namespace nashville;
using namespace std::string_literals;

TEST(chord, user_input)
{
    model::chord c;
    model::chord ref;
    for (const auto& num : { "1", "2", "3", "4", "5", "6", "7" })
    {
        EXPECT_NO_THROW(c.parse_user_input(num));
        ref.number(num[0] - '0');
        EXPECT_EQ(c, ref);
    }
    EXPECT_THROW(c.parse_user_input("0"), std::invalid_argument);
    EXPECT_THROW(c.parse_user_input("8"), std::invalid_argument);
    EXPECT_THROW(c.parse_user_input("9"), std::invalid_argument);
    EXPECT_NO_THROW(c.parse_user_input("b5"));
    ref.number(5).step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("#7"));
    ref.number(7).step(model::chord::flat_sharp::SHARP);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("6-"));
    ref.clear()
       .number(6)
       .mode(model::chord::type::MINOR);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("7dim"));
    ref.clear()
       .number(7)
       .mode(model::chord::type::DIMINISHED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("4+"));
    ref.clear()
       .number(4)
       .mode(model::chord::type::AUGMENTED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("b3-7"));
    ref.clear()
       .number(3)
       .step(model::chord::flat_sharp::FLAT)
       .mode(model::chord::type::MINOR)
       .extensions("7");
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("#7diddly"));
    ref.clear()
       .number(7)
       .step(model::chord::flat_sharp::SHARP)
       .extensions("diddly");
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("6/1"));
    ref.clear()
       .number(6)
       .bass_note(1);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("3diddly/b7"));
    ref.clear()
       .number(3)
       .extensions("diddly")
       .bass_note(7)
       .bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("3dimdiddly/b7"));
    ref.mode(model::chord::type::DIMINISHED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("#2-/b5"));
    ref.clear()
       .number(2)
       .mode(model::chord::type::MINOR)
       .step(model::chord::flat_sharp::SHARP)
       .bass_note(5)
       .bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_THROW(c.parse_user_input("5:"), std::invalid_argument);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:s"));
    ref.clear().number(1).duration(model::chord::time::SIXTEENTH);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:S"));
    EXPECT_EQ(c, ref);
    // There is no dotted sixteenth, but we allow it and ignore
    EXPECT_NO_THROW(c.parse_user_input("1:s."));
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:e"));
    ref.duration(model::chord::time::EIGHTH);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:E"));
    ref.duration(model::chord::time::EIGHTH);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:e."));
    ref.duration(model::chord::time::DOTTED_EIGHTH);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:E."));
    ref.duration(model::chord::time::DOTTED_EIGHTH);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:q"));
    ref.duration(model::chord::time::QUARTER);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:Q"));
    ref.duration(model::chord::time::QUARTER);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:q."));
    ref.duration(model::chord::time::DOTTED_QUARTER);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:Q."));
    ref.duration(model::chord::time::DOTTED_QUARTER);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:h"));
    ref.duration(model::chord::time::HALF);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:H"));
    ref.duration(model::chord::time::HALF);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:h."));
    ref.duration(model::chord::time::DOTTED_HALF);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:H."));
    ref.duration(model::chord::time::DOTTED_HALF);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:w"));
    ref.duration(model::chord::time::WHOLE);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:w"));
    ref.duration(model::chord::time::WHOLE);
    EXPECT_EQ(c, ref);
    // There is no dotted whole, but we allow it and ignore
    EXPECT_NO_THROW(c.parse_user_input("1:w."));
    EXPECT_EQ(c, ref);
    EXPECT_THROW(c.parse_user_input("mydoghasfleas"), std::invalid_argument);
    EXPECT_THROW(c.parse_user_input("7dim:blah"), std::invalid_argument);
    // Check the pushed, tied, diamond and staccato settings
    ref.clear().number(1).is_staccato(true);
    EXPECT_NO_THROW(c.parse_user_input("s:1"));
    EXPECT_EQ(c, ref);
    ref.is_diamond(true);
    EXPECT_NO_THROW(c.parse_user_input("d:1"));
    EXPECT_EQ(c, ref);
    ref.is_pushed(true).is_diamond(false);
    EXPECT_NO_THROW(c.parse_user_input("p:1"));
    EXPECT_EQ(c, ref);
    ref.is_tied(true).is_pushed(false);
    EXPECT_NO_THROW(c.parse_user_input("t:1"));
    EXPECT_EQ(c, ref);
    ref.is_diamond(true).is_pushed(true).is_tied(true);
    EXPECT_NO_THROW(c.parse_user_input("dpt:1"));
    EXPECT_EQ(c, ref);
    ref.is_diamond(false).is_pushed(true).is_tied(false).is_staccato(true);
    EXPECT_NO_THROW(c.parse_user_input("sdpt:1"));
    EXPECT_EQ(c, ref);
    EXPECT_THROW(c.parse_user_input("q:1"), std::invalid_argument);
    EXPECT_THROW(c.parse_user_input(":1"), std::invalid_argument);
    ref.clear()
       .number(model::chord::REST);
    EXPECT_NO_THROW(c.parse_user_input("r"));
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("R"));
    EXPECT_EQ(c, ref);
}

TEST(chord, to_user_input)
{
    model::chord c;
    EXPECT_EQ(std::string(), c.to_user_input());
    EXPECT_THROW(c.number(8), std::invalid_argument);
    for (const auto& num : { 1, 2, 3, 4, 5, 6, 7 })
        EXPECT_EQ(std::to_string(num), c.clear().number(num).to_user_input());
    c.clear()
     .step(model::chord::flat_sharp::FLAT)
     .number(5);
    EXPECT_EQ("b5"s, c.to_user_input());
    c.step(model::chord::flat_sharp::SHARP);
    EXPECT_EQ("#5"s, c.to_user_input());
    c.clear()
     .number(6)
     .mode(model::chord::type::MINOR);
    EXPECT_EQ("6-"s, c.to_user_input());
    c.mode(model::chord::type::DIMINISHED);
    EXPECT_EQ("6dim"s, c.to_user_input());
    c.mode(model::chord::type::AUGMENTED);
    EXPECT_EQ("6+"s, c.to_user_input());
    c.clear()
     .step(model::chord::flat_sharp::FLAT)
     .number(2)
     .mode(model::chord::type::MINOR)
     .extensions("7");
    EXPECT_EQ("b2-7"s, c.to_user_input());
    c.clear()
     .step(model::chord::flat_sharp::SHARP)
     .number(3)
     .extensions("7");
    EXPECT_EQ("#37"s, c.to_user_input());
    c.clear()
     .number(6)
     .mode(model::chord::type::MINOR)
     .bass_note(1);
    EXPECT_EQ("6-/1"s, c.to_user_input());
    c.bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ("6-/b1"s, c.to_user_input());
    c.number(6).bass_note_step(model::chord::flat_sharp::SHARP);
    EXPECT_EQ("6-/#1"s, c.to_user_input());
    c.clear()
     .number(1)
     .duration(model::chord::time::SIXTEENTH);
    EXPECT_EQ("1:s"s, c.to_user_input());
    c.duration(model::chord::time::EIGHTH);
    EXPECT_EQ("1:e"s, c.to_user_input());
    c.duration(model::chord::time::DOTTED_EIGHTH);
    EXPECT_EQ("1:e."s, c.to_user_input());
    c.duration(model::chord::time::QUARTER);
    EXPECT_EQ("1:q"s, c.to_user_input());
    c.duration(model::chord::time::DOTTED_QUARTER);
    EXPECT_EQ("1:q."s, c.to_user_input());
    c.duration(model::chord::time::HALF);
    EXPECT_EQ("1:h"s, c.to_user_input());
    c.duration(model::chord::time::DOTTED_HALF);
    EXPECT_EQ("1:h."s, c.to_user_input());
    c.duration(model::chord::time::WHOLE);
    EXPECT_EQ("1:w"s, c.to_user_input());
    c.clear()
     .number(1)
     .is_diamond(true);
    EXPECT_EQ("d:1"s, c.to_user_input());
    c.is_diamond(false).is_pushed(true);
    EXPECT_EQ("p:1"s, c.to_user_input());
    c.is_pushed(false).is_staccato(true);
    EXPECT_EQ("s:1"s, c.to_user_input());
    c.is_staccato(false).is_tied(true);
    EXPECT_EQ("t:1"s, c.to_user_input());
    c.is_staccato(true);
    EXPECT_EQ("s:1"s, c.to_user_input());
    c.is_pushed(true);
    EXPECT_EQ("ps:1"s, c.to_user_input());
    c.is_tied(true);
    EXPECT_EQ("pt:1"s, c.to_user_input());
    c.is_pushed(false).is_diamond(true);
    EXPECT_EQ("dt:1"s, c.to_user_input());
    c.is_pushed(true);
    EXPECT_EQ("dpt:1"s, c.to_user_input());
    c.is_staccato(true);
    EXPECT_EQ("ps:1"s, c.to_user_input());
    c.clear()
     .number(model::chord::REST);
    EXPECT_EQ("r"s, c.to_user_input());
}
