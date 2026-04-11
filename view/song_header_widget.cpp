#include "song_header_widget.hpp"
#include <QVBoxLayout>
#include <QPainter>
#include <QFontMetricsF>
#include <QPrintDialog>
#include <cmath>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// song_widget
// ---------------------------------------------------------------------------
song_widget::song_widget(const model::song& song, QWidget* parent)
    : QWidget(parent), song_(song)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    body_ = new song_body_widget(song_, scroll_);
    scroll_->setWidget(body_);

    layout->addWidget(scroll_, 1);
    setLayout(layout);
}

void song_widget::refresh()
{
    body_->rebuild();
}

void song_widget::print(QPrinter* printer)
{
    QPrintDialog dialog(printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPainter painter(printer);
    painter.setRenderHint(QPainter::Antialiasing);

    QRectF pageRect = printer->pageRect(QPrinter::DevicePixel);
    body_->paint_to_rect(painter, pageRect);
}

} // namespace nashville::view
