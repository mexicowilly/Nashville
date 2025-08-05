#include "chord.hpp"
#include <QGridLayout>
#include <QLabel>

namespace nashville::widgets
{

chord::chord(QWidget* parent)
    : QWidget(parent),
      layout_(new QGridLayout())
{
    setLayout(layout_);
    chord_text_ = new QLabel(this);
    layout_->addWidget(chord_text_, 0, 0);
}

}
