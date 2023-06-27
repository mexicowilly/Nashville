#pragma once

#include <QtWidgets/QLineEdit>

namespace nashville::ui
{

class title_text : public QLineEdit
{
    Q_OBJECT

public:
    title_text();

protected:
    virtual void mousePressEvent(QMouseEvent* event) override;
};

}