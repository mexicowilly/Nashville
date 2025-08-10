#pragma once

#include <QWidget>
#include <QHBoxLayout>
#include "../model/bar.hpp"

namespace nashville::widgets
{

class bar : public QWidget
{
    Q_OBJECT

public:
    bar(QWidget* parent, model::bar& mdl);

    bool empty() const;

private:
    model::bar& model_;
    QHBoxLayout* layout_;
};

inline bool bar::empty() const
{
    return model_.empty();
}

}
