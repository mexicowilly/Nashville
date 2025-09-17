#pragma once

#include <QtWidgets/QLineEdit>

namespace nashville::widgets
{

class title_text : public QLineEdit
{
    Q_OBJECT

public:
    title_text();

signals:
    void title_changed(const QString& t);

protected:
    virtual void mouseReleaseEvent(QMouseEvent* event) override;

private slots:
    void edit_finished();
};

}
