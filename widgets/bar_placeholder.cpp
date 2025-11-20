#include "bar_placeholder.hpp"
#include "../measures.hpp"

namespace nashville::widgets
{

bar_placeholder::bar_placeholder(QWidget* parent)
    : QFrame(parent)
{
    setFixedWidth(measures::inches_wide(bar::DEFAULT_MINIMUM_WIDTH));
    setMinimumHeight(measures::inches_tall(bar::DEFAULT_MINIMUM_HEIGHT));
    setFrameShape(QFrame::Box);
}

}
