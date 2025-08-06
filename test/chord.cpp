#include <gtest/gtest.h>
#include "../model/chord.hpp"

using namespace nashville;

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
    c.reset();
    EXPECT_NO_THROW(c.parse_user_input("b5"));
    ref.number(5).step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    c.reset();
    EXPECT_NO_THROW(c.parse_user_input("#7"));
    ref.number(7).step(model::chord::flat_sharp::SHARP);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("6-"));
    ref.reset()
       .number(6)
       .mode(model::chord::type::MINOR);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("7dim"));
    ref.reset()
       .number(7)
       .mode(model::chord::type::DIMINISHED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("4+"));
    ref.reset()
       .number(4)
       .mode(model::chord::type::AUGMENTED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("b3-7"));
    ref.reset()
       .number(3)
       .step(model::chord::flat_sharp::FLAT)
       .mode(model::chord::type::MINOR)
       .extensions("7");
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("#7diddly"));
    ref.reset()
       .number(7)
       .step(model::chord::flat_sharp::SHARP)
       .extensions("diddly");
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("6/1"));
    ref.reset()
       .number(6)
       .bass_note(1);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("3diddly/b7"));
    ref.reset()
       .number(3)
       .extensions("diddly")
       .bass_note(7)
       .bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("3dimdiddly/b7"));
    ref.mode(model::chord::type::DIMINISHED);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("#2-/b5"));
    ref.reset()
       .number(2)
       .mode(model::chord::type::MINOR)
       .step(model::chord::flat_sharp::SHARP)
       .bass_note(5)
       .bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(c, ref);
    EXPECT_NO_THROW(c.parse_user_input("1:s"));
    ref.reset().number(1).duration(model::chord::time::SIXTEENTH);
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
}

TEST(chord, to_user_input)
{
    model::chord c;
    EXPECT_EQ(std::string(), c.to_user_input());
    EXPECT_THROW(c.number(0), std::invalid_argument);
    EXPECT_THROW(c.number(8), std::invalid_argument);
    for (const auto& num : { 1, 2, 3, 4, 5, 6, 7 })
        EXPECT_EQ(std::to_string(num), c.reset().number(num).to_user_input());
    c.reset()
     .step(model::chord::flat_sharp::FLAT)
     .number(5);
    EXPECT_EQ(std::string("b5"), c.to_user_input());
    c.step(model::chord::flat_sharp::SHARP);
    EXPECT_EQ(std::string("#5"), c.to_user_input());
    c.reset()
     .number(6)
     .mode(model::chord::type::MINOR);
    EXPECT_EQ(std::string("6-"), c.to_user_input());
    c.mode(model::chord::type::DIMINISHED);
    EXPECT_EQ(std::string("6dim"), c.to_user_input());
    c.mode(model::chord::type::AUGMENTED);
    EXPECT_EQ(std::string("6+"), c.to_user_input());
    c.reset()
     .step(model::chord::flat_sharp::FLAT)
     .number(2)
     .mode(model::chord::type::MINOR)
     .extensions("7");
    EXPECT_EQ(std::string("b2-7"), c.to_user_input());
    c.reset()
     .step(model::chord::flat_sharp::SHARP)
     .number(3)
     .extensions("7");
    EXPECT_EQ(std::string("#37"), c.to_user_input());
    c.reset()
     .number(6)
     .mode(model::chord::type::MINOR)
     .bass_note(1);
    EXPECT_EQ(std::string("6-/1"), c.to_user_input());
    c.bass_note_step(model::chord::flat_sharp::FLAT);
    EXPECT_EQ(std::string("6-/b1"), c.to_user_input());
    c.number(6).bass_note_step(model::chord::flat_sharp::SHARP);
    EXPECT_EQ(std::string("6-/#1"), c.to_user_input());
    c.reset()
     .number(1)
     .duration(model::chord::time::SIXTEENTH);
    EXPECT_EQ(std::string("1:s"), c.to_user_input());
    c.duration(model::chord::time::EIGHTH);
    EXPECT_EQ(std::string("1:e"), c.to_user_input());
    c.duration(model::chord::time::DOTTED_EIGHTH);
    EXPECT_EQ(std::string("1:e."), c.to_user_input());
    c.duration(model::chord::time::QUARTER);
    EXPECT_EQ(std::string("1:q"), c.to_user_input());
    c.duration(model::chord::time::DOTTED_QUARTER);
    EXPECT_EQ(std::string("1:q."), c.to_user_input());
    c.duration(model::chord::time::HALF);
    EXPECT_EQ(std::string("1:h"), c.to_user_input());
    c.duration(model::chord::time::DOTTED_HALF);
    EXPECT_EQ(std::string("1:h."), c.to_user_input());
    c.duration(model::chord::time::WHOLE);
    EXPECT_EQ(std::string("1:w"), c.to_user_input());
}
