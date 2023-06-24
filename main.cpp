#include "bar.hpp"
int main()
{
    nashville::model::chord ch(1);
    nashville::model::bar b;
    b.add_chord(std::move(ch));
}