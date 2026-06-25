#include "song_widget.hpp"
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
song_widget::song_widget(model::song& song, QWidget* parent)
    : QWidget(parent), song_(song)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    scroll_ = new QScrollArea(this);
    scroll_->setWidgetResizable(true);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // QScrollArea is a QFrame and defaults to a 1px styled-panel border, which
    // (under the app's black palette) rendered as a hard line boxing in the
    // song.  Drop it: the Chrome tab's grey delineation is the only separation
    // the song surface needs.
    scroll_->setFrameShape(QFrame::NoFrame);

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

    QRectF page_rect = printer->pageRect(QPrinter::DevicePixel);
    body_->paint_to_rect(painter, page_rect);
}

} // namespace nashville::view
