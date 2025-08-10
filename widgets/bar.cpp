#include "bar.hpp"

namespace nashville::widgets
{

bar::bar(QWidget* parent, model::bar& mdl)
    : QWidget(parent),
      model_(mdl),
      layout_(new QHBoxLayout())
{
    setLayout(layout_);
}

}
