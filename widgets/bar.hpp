#pragma once

#include <QFrame>
#include "../model/bar.hpp"

namespace nashville::widgets
{

class bar : public QFrame
{
    Q_OBJECT

public:
    static inline constexpr int DEFAULT_MINIMUM_WIDTH = 100;
    static inline constexpr int DEFAULT_MINIMUM_HEIGHT = 30;

    bar(QWidget* parent = nullptr);
    bar(QWidget* parent, const model::bar& mdl);

    bool empty() const;

protected:
    virtual void focusInEvent(QFocusEvent* evt) override;
    virtual void focusOutEvent(QFocusEvent* evt) override;
    virtual void mouseReleaseEvent(QMouseEvent* evt) override;

private:
    model::bar model_;
};

inline bool bar::empty() const
{
    return model_.empty();
}

}
