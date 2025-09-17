#include "song.hpp"
#include "bar_placeholder.hpp"
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
      model_(mdl),
      bar_grid_(new QGridLayout())
{
    bar_grid_->setHorizontalSpacing(10);
    bar_grid_->setVerticalSpacing(10);
    auto pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    setAutoFillBackground(true);
    setPalette(pal);   
    auto grid = new QGridLayout();
    setLayout(grid);
    auto tit = new title_text();
    connect(tit, &title_text::title_changed, this, &song::title_changed);
    grid->addWidget(tit, 0, 0, 1, -1, Qt::AlignCenter);
    auto kc = new key_column();
    connect(kc, &key_column::key_changed, this, &song::key_changed);
    connect(kc, &key_column::tempo_changed, this, &song::tempo_changed);
    connect(kc, &key_column::time_signature_changed, this, &song::time_signature_changed);
    grid->addWidget(kc, 1, 0, -1, 1, Qt::AlignTop);
    // Column zero of the bar grid belongs to the section names. This allows
    // the section names to line up with the bars at which they start.
    grid->addLayout(bar_grid_, 1, 1, -1, -1);
    bar_grid_->addWidget(new bar(), 1, 1);
    for (int i = 2; i <= model_.bars_per_line(); i++)
        bar_grid_->addWidget(new bar_placeholder(), 1, i);
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
