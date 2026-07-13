#include "song_widget.hpp"
#include <QVBoxLayout>
#include <QPainter>
#include <QFontMetricsF>
#include <QPrintDialog>
#include <QPageSize>
#include <QPalette>
#include <QColor>
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
    // The body is a fixed-width "page", not a stretchy widget: don't let the
    // scroll area resize it to the viewport (that would defeat the whole
    // WYSIWYG-page idea).  Instead the body keeps its page width and we
    // centre it, letting the desk-gray backdrop show in the margins around
    // it on wide windows.
    scroll_->setWidgetResizable(false);
    scroll_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    // QScrollArea is a QFrame and defaults to a 1px styled-panel border, which
    // (under the app's black palette) rendered as a hard line boxing in the
    // song.  Drop it: the Chrome tab's grey delineation is the only separation
    // the song surface needs.
    scroll_->setFrameShape(QFrame::NoFrame);
    // The viewport shows the desk-gray around the centred page; match it so
    // there's no white flash at the edges before the body paints.
    scroll_->viewport()->setAutoFillBackground(true);
    {
        QPalette vp = scroll_->viewport()->palette();
        vp.setColor(QPalette::Window, QColor(0xE4, 0xE4, 0xE7));
        scroll_->viewport()->setPalette(vp);
    }

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
    // Match the printer's paper to the app-wide page setup the chart was
    // laid out for, so what prints matches the on-screen preview 1:1 rather
    // than being scaled to whatever the printer's default paper happened to
    // be.  Margins are baked into the chart layout itself (the content is
    // already inset from the sheet edges by page_geo_.margins), so we print
    // full-bleed here — a second layer of printer margins would double the
    // inset.
    const auto& geo = body_->current_page_geometry();
    printer->setFullPage(true);
    printer->setPageSize(QPageSize(
        QSizeF(geo.size.width(), geo.size.height()),
        QPageSize::Point));

    QPrintDialog dialog(printer, this);
    if (dialog.exec() != QDialog::Accepted)
        return;

    QPainter painter(printer);
    painter.setRenderHint(QPainter::Antialiasing);

    const int pages = body_->page_count();
    for (int p = 0; p < pages; ++p)
    {
        if (p > 0)
            printer->newPage();
        // Paint page p into the printer's full physical page rect.  The
        // body scales our page width onto this rect (1:1 when the paper
        // size matches, which we just set it to).
        const QRectF page_rect = printer->pageRect(QPrinter::DevicePixel);
        body_->paint_page_to_rect(painter, p, page_rect);
    }
}

} // namespace nashville::view
