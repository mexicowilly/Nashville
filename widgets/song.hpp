#pragma once

#include "../model/song.hpp"
#include <QWidget>
#include <QGridLayout>

namespace nashville::widgets
{

class song : public QWidget
{
    Q_OBJECT

public:
    song(QWidget* parent = nullptr);
    song(QWidget* parent, const model::song& mdl);

private slots:
    void key_changed(const QString& key);
    void name_changed(const QString& n);
    void tempo_changed(const QString& temp);
    void time_signature_changed(const QString& ts);

private:
    model::song model_;
    QGridLayout* bar_grid_;
};

}
