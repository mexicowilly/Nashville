#pragma once

#include <QFrame>
#include "../model/bar.hpp"

namespace nashville::widgets
{

class bar : public QFrame
{
    Q_OBJECT

public:
    bar(QWidget* parent);
    bar(QWidget* parent, const model::bar& mdl);

    bool empty() const;

protected:
    virtual void focusInEvent(QFocusEvent* evt) override;
    virtual void focusOutEvent(QFocusEvent* evt) override;
    virtual void mousePressEvent(QMouseEvent* evt) override;

private:
    model::bar model_;
};

inline bool bar::empty() const
{
    return model_.empty();
}

}
