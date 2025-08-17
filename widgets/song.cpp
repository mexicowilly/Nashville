#include "song.hpp"
#include "key_column.hpp"
#include "title_text.hpp"
#include <QGridLayout>

namespace nashville::widgets
{

song::song(QWidget* parent)
    : song(parent, model::song())
{
}

song::song(QWidget* parent, const model::song& mdl)
    : QWidget(parent),
      model_(mdl)
{
    auto grid = new QGridLayout();
    setLayout(grid);
    auto tit = new title_text();
    connect(tit, &title_text::title_changed, this, &song::title_changed);
    grid->addWidget(tit, 0, 0, -1, 1, Qt::AlignCenter);
    auto kc = new key_column();
    connect(kc, &key_column::key_changed, this, &song::key_changed);
    connect(kc, &key_column::tempo_changed, this, &song::tempo_changed);
    connect(kc, &key_column::time_signature_changed, this, &song::time_signature_changed);
    grid->addWidget(kc, 0, 0, 1, -1, Qt::AlignTop);
}

void song::key_changed(const QString& key)
{
    model_.key(key.toStdString());
}

void song::tempo_changed(const QString& temp)
{
}

void song::time_signature_changed(const QString& ts)
{
}

void song::title_changed(const QString& t)
{
    model_.title(t.toStdString());
}

}
