#pragma once

#include "bar.hpp"

namespace nashville::widgets
{

class bar_placeholder : public QFrame
{
    Q_OBJECT

public:
    bar_placeholder(QWidget* parent = nullptr);
};

}
