#include "bar_placeholder.hpp"

namespace nashville::widgets
{

bar_placeholder::bar_placeholder(QWidget* parent)
    : QWidget(parent)
{
    setMinimumWidth(bar::DEFAULT_MINIMUM_WIDTH);
    setMinimumHeight(bar::DEFAULT_MINIMUM_HEIGHT);
}

}
