#include "song_body_widget.hpp"
#include "margin_renderer.hpp"
#include <QPainter>
#include <QMouseEvent>
#include <QFontDatabase>
#include <QApplication>
#include <cmath>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
SongBodyWidget::SongBodyWidget(const model::song& song, QWidget* parent)
    : QWidget(parent), song_(song)
{
    setMouseTracking(true);
    initFonts();
    rebuild();
}

void SongBodyWidget::initFonts()
{
    int bravuraId = QFontDatabase::addApplicationFont(":/fonts/Bravura.otf");
    QString musicFamily = (bravuraId != -1)
                          ? QFontDatabase::applicationFontFamilies(bravuraId).first()
                          : QApplication::font().family();

    fonts_.number = QFont("Georgia", 18, QFont::Normal);
    fonts_.modifier     = QFont("Georgia", 11);
    fonts_.articulation = QFont("Georgia", 12);
    fonts_.music        = QFont(musicFamily, 14);
}

// ---------------------------------------------------------------------------
// rebuild
// ---------------------------------------------------------------------------
void SongBodyWidget::rebuild()
{
    QRectF contentRect(marginWidth_ + kContentPadding,
                       kContentPadding,
                       std::max(0.0, width()  - marginWidth_ - kContentPadding * 2),
                       std::max(0.0, height() - kContentPadding * 2));
    computeLayout(contentRect);
    update();
}

// ---------------------------------------------------------------------------
// computeLayout
// ---------------------------------------------------------------------------
void SongBodyWidget::computeLayout(const QRectF& contentRect)
{
    lines_.clear();

    if (song_.empty())
        return;

    const auto& bars       = song_.bars();
    const unsigned bplPref = song_.bars_per_line();

    // --- Pass 1: group bars into lines ---
    // Rules:
    //   - is_eol_ forces a break after this bar
    //   - extends_line_ suppresses the count-break for this bar
    //   - otherwise break when countInLine reaches bplPref

    struct RawLine { std::vector<const model::bar*> bars; };
    std::vector<RawLine> rawLines;
    RawLine current;
    unsigned countInLine = 0;

    for (const auto& b : bars)
    {
        current.bars.push_back(&b);
        countInLine++;

        bool forceBreak = b.is_eol();
        bool countBreak = (countInLine >= bplPref) && !b.extends_line();

        if (forceBreak || countBreak)
        {
            rawLines.push_back(std::move(current));
            current.bars.clear();
            countInLine = 0;
        }
    }
    if (!current.bars.empty())
        rawLines.push_back(std::move(current));

    qreal plainH    = plainBarHeight();
    qreal durationH = durationBarHeight();
    qreal secLabelH = sectionLabelHeight();

    // --- Pass 2: compute per-column widths ---
    // Column index = bar position within its line (0-based).
    // Every bar in the same column gets the same width = max natural width in that column.
    constexpr qreal kBarPadding = 16.0;
    std::vector<qreal> colWidths;  // indexed by column (position within line)
    for (const auto& raw : rawLines)
    {
        bool lineIsDur = false;
        for (const auto* b : raw.bars)
            if (!b->empty() && b->chords().front().duration().has_value())
                { lineIsDur = true; break; }
        qreal barH = lineIsDur ? durationH : plainH;

        for (std::size_t j = 0; j < raw.bars.size(); ++j)
        {
            qreal w = BarRenderer::widthHint(*raw.bars[j], barH, fonts_) + kBarPadding;
            if (j >= colWidths.size())
                colWidths.push_back(w);
            else
                colWidths[j] = std::max(colWidths[j], w);
        }
    }

    // --- Pass 3: compute geometry ---
    qreal y = contentRect.top();

    for (const auto& raw : rawLines)
    {
        LineLayout line;

        // Determine line-level flags
        for (const auto* b : raw.bars)
        {
            if (!b->empty() && b->chords().front().duration().has_value())
                line.isDurationMode = true;
            if (b->section() && !line.sectionLabel)
                line.sectionLabel = QString::fromStdString(*b->section());
        }

        qreal barH    = line.isDurationMode ? durationH : plainH;
        qreal lineTop = y + (line.sectionLabel ? secLabelH : 0.0);

        qreal x = contentRect.left();
        for (std::size_t j = 0; j < raw.bars.size(); ++j)
        {
            const model::bar* b = raw.bars[j];
            qreal barW = colWidths[j];

            BarLayout bl;
            bl.bar           = b;
            bl.rect          = QRectF(x, lineTop, barW, barH);
            bl.isDurationMode = !b->empty()
                                && b->chords().front().duration().has_value();

            // Continuation dot: appears after the last "normal count" bar,
            // i.e. bar at index (bplPref - 1) when the next bar extends the line.
            if (j == bplPref - 1
                && (j + 1) < raw.bars.size()
                && raw.bars[j + 1]->extends_line())
            {
                bl.showContinuationDot = true;
            }

            line.bars.push_back(bl);
            x += barW + kInterBarSpacing;
        }

        qreal lineH = barH + (line.sectionLabel ? secLabelH : 0.0);
        line.rect   = QRectF(contentRect.left(), y, contentRect.width(), lineH);
        lines_.push_back(std::move(line));

        y += lineH + kLineSpacing;
    }

    setMinimumHeight(static_cast<int>(y + kContentPadding));
}

// ---------------------------------------------------------------------------
// Height helpers
// ---------------------------------------------------------------------------
qreal SongBodyWidget::plainBarHeight() const
{
    QFontMetricsF fm(fonts_.number);
    return fm.height() / ChordRenderer::kNumberZoneRatio;
}

qreal SongBodyWidget::durationBarHeight() const
{
    QFontMetricsF musicFm(fonts_.music);
    return plainBarHeight() + musicFm.height() + 4.0 + 8.0;
}

qreal SongBodyWidget::sectionLabelHeight() const
{
    QFontMetricsF fm(fonts_.modifier);
    return fm.height() + 6.0;
}

// ---------------------------------------------------------------------------
// paintEvent
// ---------------------------------------------------------------------------
void SongBodyWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), Qt::white);

    paintMargin(painter, QRectF(0, 0, marginWidth_, height()));
    paintDivider(painter);

    for (const auto& line : lines_)
        paintLine(painter, line);
}

// ---------------------------------------------------------------------------
// paintMargin
// ---------------------------------------------------------------------------
void SongBodyWidget::paintMargin(QPainter& painter, const QRectF& marginRect) const
{
    MarginRenderer::paint(painter, marginRect, song_);
}

// ---------------------------------------------------------------------------
// paintDivider
// ---------------------------------------------------------------------------
void SongBodyWidget::paintDivider(QPainter& painter) const
{
    painter.save();
    painter.setPen(QPen(QColor(180, 180, 180), 1));
    painter.drawLine(marginWidth_, 0, marginWidth_, height());
    painter.restore();
}

// ---------------------------------------------------------------------------
// paintLine
// ---------------------------------------------------------------------------
void SongBodyWidget::paintLine(QPainter& painter, const LineLayout& line) const
{
    if (line.sectionLabel)
        paintSectionLabel(painter, *line.sectionLabel, line.rect);

    for (const auto& bl : line.bars)
    {
        BarRenderer::paint(painter, bl.rect, *bl.bar, fonts_, line.isDurationMode);

        if (bl.showContinuationDot)
            paintContinuationDot(painter, bl.rect);
    }
}

// ---------------------------------------------------------------------------
// paintSectionLabel
// ---------------------------------------------------------------------------
void SongBodyWidget::paintSectionLabel(QPainter& painter,
                                        const QString& label,
                                        const QRectF& lineRect) const
{
    painter.save();

    QFont labelFont = fonts_.modifier;
    labelFont.setBold(true);
    labelFont.setItalic(true);
    painter.setFont(labelFont);
    QFontMetricsF fm(labelFont);

    qreal labelY = lineRect.top() + fm.ascent();
    painter.drawText(QPointF(lineRect.left(), labelY), label);

    qreal ruleY = lineRect.top() + fm.height() + 2.0;
    painter.setPen(QPen(QColor(180, 180, 180), 0.75));
    painter.drawLine(QPointF(lineRect.left(),  ruleY),
                     QPointF(lineRect.right(), ruleY));

    painter.restore();
}

// ---------------------------------------------------------------------------
// paintContinuationDot
// ---------------------------------------------------------------------------
void SongBodyWidget::paintContinuationDot(QPainter& painter,
                                           const QRectF& precedingBarRect) const
{
    painter.save();
    constexpr qreal dotR = 3.0;
    qreal cx = precedingBarRect.right() + kInterBarSpacing / 2.0;
    qreal cy = precedingBarRect.center().y();
    painter.setBrush(Qt::black);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(QPointF(cx, cy), dotR, dotR);
    painter.restore();
}

// ---------------------------------------------------------------------------
// resizeEvent
// ---------------------------------------------------------------------------
void SongBodyWidget::resizeEvent(QResizeEvent*)
{
    rebuild();
}

// ---------------------------------------------------------------------------
// Draggable divider
// ---------------------------------------------------------------------------
bool SongBodyWidget::nearDivider(int x) const
{
    return std::abs(x - marginWidth_) <= kDividerHitWidth;
}

void SongBodyWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && nearDivider(event->pos().x()))
    {
        draggingDivider_ = true;
        dragStartX_      = event->pos().x();
        dragStartMargin_ = marginWidth_;
        setCursor(Qt::SplitHCursor);
    }
}

void SongBodyWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (draggingDivider_)
    {
        int delta     = event->pos().x() - dragStartX_;
        int newMargin = qBound(kMinMarginWidth,
                               dragStartMargin_ + delta,
                               kMaxMarginWidth);
        if (newMargin != marginWidth_)
        {
            marginWidth_ = newMargin;
            rebuild();
        }
    }
    else
    {
        setCursor(nearDivider(event->pos().x()) ? Qt::SplitHCursor
                                                : Qt::ArrowCursor);
    }
}

void SongBodyWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && draggingDivider_)
    {
        draggingDivider_ = false;
        setCursor(nearDivider(event->pos().x()) ? Qt::SplitHCursor
                                                : Qt::ArrowCursor);
    }
}

// ---------------------------------------------------------------------------
// paintToRect — for printing
// ---------------------------------------------------------------------------
void SongBodyWidget::paintToRect(QPainter& painter, const QRectF& pageRect) const
{
    painter.save();

    qreal scaleX = pageRect.width()  / static_cast<qreal>(width());
    qreal scaleY = pageRect.height() / static_cast<qreal>(height());
    qreal scale  = std::min(scaleX, scaleY);

    painter.translate(pageRect.left(), pageRect.top());
    painter.scale(scale, scale);

    QRectF myRect(0, 0, width(), height());
    painter.fillRect(myRect, Qt::white);
    MarginRenderer::paint(painter, QRectF(0, 0, marginWidth_, height()), song_);
    paintDivider(painter);
    for (const auto& line : lines_)
        paintLine(painter, line);

    painter.restore();
}

} // namespace nashville::view
