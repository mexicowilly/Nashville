#include "main_window.hpp"
#include "side_panel.hpp"
#include "song_tab.hpp"
#include "modal_overlay.hpp"
#include "../database.hpp"
#include "../model/playlist.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QTabWidget>
#include <QTabBar>
#include <QToolButton>
#include <QLabel>
#include <QListWidget>
#include <QPalette>
#include <QAbstractButton>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QPainterPath>
#include <QEnterEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QDate>
#include <algorithm>
#include <utility>

namespace nashville::view
{

namespace
{
// Three thin black horizontal lines on a transparent ground — a minimal
// hamburger glyph drawn as crisp 1px rules (rather than the heavy U+2630
// character), sized to sit unobtrusively in the tab bar's corner.
QIcon make_hamburger_icon()
{
    constexpr int w = 16, h = 12;
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        const QColor ink(0, 0, 0);
        const int x = 2, line_w = w - 4;   // small side inset
        p.fillRect(x, 1, line_w, 1, ink);  // three evenly spaced 1px rules
        p.fillRect(x, 5, line_w, 1, ink);
        p.fillRect(x, 9, line_w, 1, ink);
    }
    return QIcon(pm);
}
} // namespace

// ---------------------------------------------------------------------------
// tab_close_button — custom close button for tab chrome
// ---------------------------------------------------------------------------
// Paints a small "x" centred in the button.  A thin 1px circle is drawn
// around it only while the mouse is hovering, giving a subtle affordance
// without cluttering the tab bar at rest.  The x itself is always faintly
// visible so the button remains discoverable without requiring hover first.
class tab_close_button : public QAbstractButton
{
public:
    explicit tab_close_button(QWidget* parent = nullptr)
        : QAbstractButton(parent)
    {
        setFixedSize(16, 16);
        setCursor(Qt::ArrowCursor);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_Hover, true);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF  r      = QRectF(rect());
        const QPointF centre = r.center();
        const qreal   radius = r.width() / 2.0 - 1.5;   // 1.5px inset from edge

        // Circle: only while hovered
        if (underMouse())
        {
            p.setPen(QPen(QColor(120, 120, 120), 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(centre, radius, radius);
        }

        // x glyph — only drawn while hovered; invisible at rest so the
        // tab chrome stays uncluttered until the user moves over the button.
        if (!underMouse())
            return;
        p.setPen(QPen(QColor(60, 60, 60), 1.2, Qt::SolidLine, Qt::RoundCap));

        const qreal arm = radius * 0.42;   // half-length of each arm
        p.drawLine(QPointF(centre.x() - arm, centre.y() - arm),
                   QPointF(centre.x() + arm, centre.y() + arm));
        p.drawLine(QPointF(centre.x() + arm, centre.y() - arm),
                   QPointF(centre.x() - arm, centre.y() + arm));
    }

    // Repaint on enter/leave so the circle appears/disappears instantly.
    void enterEvent(QEnterEvent* e) override { QAbstractButton::enterEvent(e); update(); }
    void leaveEvent(QEvent*      e) override { QAbstractButton::leaveEvent(e); update(); }
};

// ---------------------------------------------------------------------------
// chrome_tab_bar — Chrome-style tab strip
// ---------------------------------------------------------------------------
// A stylesheet can round a tab's *top* corners but not its bottom ones the way
// a Chrome tab needs them: there the corners flare *outward* into a concave
// "scoop" so the active tab looks fused to the page below.  That shape isn't a
// border-radius (which only curves inward), so we paint the bar ourselves.
//
// Only the current tab gets the full shape: a white fill, a 1px hairline up the
// left flare / side / rounded top / right side / right flare, and an *open*
// bottom that merges into the content pane.  A thin separator runs along the
// foot of the strip but is interrupted directly beneath the current tab, so the
// active tab reads as connected to the page while every other tab sits below an
// unbroken line.  Inactive tabs are just their label (plus a soft rounded hover
// fill) with no border at all — which is the "no lines around their borders"
// look that was asked for.
//
// The geometry is built clockwise from the bottom-left baseline using
// QPainterPath::arcTo.  Top corners are convex quarter-circles (radius
// k_radius_top); bottom corners are concave quarter-circles (radius
// k_radius_bottom) whose arc centres sit *outside* the tab body, which is what
// produces the outward flare.
class chrome_tab_bar : public QTabBar
{
public:
    explicit chrome_tab_bar(QWidget* parent = nullptr)
        : QTabBar(parent)
    {
        setDrawBase(false);       // we paint our own foot separator
        setExpanding(false);      // tabs hug their content, left-aligned
        setMouseTracking(true);   // needed so hover updates without a press
    }

protected:
    // Reserve horizontal room for the two flares plus a little breathing space
    // so labels never collide with the rounded corners, and guarantee enough
    // height for the top rounding + flare to render cleanly.
    QSize tabSizeHint(int index) const override
    {
        QSize s = QTabBar::tabSizeHint(index);
        s.setWidth(s.width() + 2 * int(k_radius_bottom) + 8);
        s.setHeight(std::max(s.height(), k_min_height));
        return s;
    }

    // With zero tabs, QTabBar::sizeHint() has no per-tab sizes to fold in and
    // collapses to a sliver a few pixels tall.  QTabWidget sizes the whole
    // tab-bar row — and therefore the corner widget (our hamburger) that
    // lives in it — off of this, so with no songs open the hamburger row
    // would shrink to near-nothing and the button would effectively vanish.
    // Floor both hints at k_min_height so the row (and hamburger) stay put
    // whether or not there are any tabs.
    QSize sizeHint() const override
    {
        QSize s = QTabBar::sizeHint();
        s.setHeight(std::max(s.height(), k_min_height));
        return s;
    }

    QSize minimumSizeHint() const override
    {
        QSize s = QTabBar::minimumSizeHint();
        s.setHeight(std::max(s.height(), k_min_height));
        return s;
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        const int h = tabAt(e->pos());
        if (h != hover_index_) { hover_index_ = h; update(); }
        QTabBar::mouseMoveEvent(e);
    }

    void leaveEvent(QEvent* e) override
    {
        if (hover_index_ != -1) { hover_index_ = -1; update(); }
        QTabBar::leaveEvent(e);
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const int   sel  = currentIndex();
        const qreal base = height() - 1.0;   // y of the foot separator

        // 1) Foot separator, broken under the current tab so that tab connects
        //    to the pane and every other tab sits below an unbroken line. With
        //    no songs open there's nothing for a divider to separate — the
        //    empty tab-bar row (just the hamburger corner widget) should read
        //    as blank, not as a stray horizontal rule.
        if (count() > 0)
        {
            p.setPen(QPen(k_outline, 1.0));
            if (sel < 0)
            {
                p.drawLine(QPointF(0, base), QPointF(width(), base));
            }
            else
            {
                const QRect sr = tabRect(sel);
                p.drawLine(QPointF(0, base),            QPointF(sr.left(), base));
                p.drawLine(QPointF(sr.left() + sr.width(), base), QPointF(width(), base));
            }
        }

        // 2) Inactive tabs: optional hover fill, then the label. No borders.
        for (int i = 0; i < count(); ++i)
        {
            if (i == sel) continue;
            paint_inactive(p, i);
        }

        // 3) Current tab last so its flares overlap the neighbours cleanly.
        if (sel >= 0)
            paint_active(p, sel, base);
    }

private:
    void paint_label(QPainter& p, int i, const QColor& color) const
    {
        const QRect r = tabRect(i);
        // Left padding clears the flare; right padding clears the flare plus
        // the custom close button that lives in the RightSide slot.
        const int left  = r.left() + int(k_radius_bottom) + 6;
        const int right = r.left() + r.width() - (int(k_radius_bottom) + 22);
        if (right <= left) return;
        const QRect textRect(QPoint(left, r.top()), QPoint(right, r.bottom()));
        const QString txt = fontMetrics().elidedText(
            tabText(i), Qt::ElideRight, textRect.width());
        p.setPen(color);
        p.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, txt);
    }

    void paint_inactive(QPainter& p, int i) const
    {
        if (i == hover_index_)
        {
            const QRect r = tabRect(i).adjusted(
                int(k_radius_bottom), 4, -int(k_radius_bottom), -2);
            QPainterPath bg;
            bg.addRoundedRect(QRectF(r), k_radius_top, k_radius_top);
            p.fillPath(bg, k_hover);
        }
        paint_label(p, i, k_text_inactive);
    }

    void paint_active(QPainter& p, int i, qreal base) const
    {
        const QRect  tr = tabRect(i);
        const qreal  L  = tr.left();
        const qreal  R  = tr.left() + tr.width();
        const qreal  T  = tr.top() + 1.0;     // 1px inset so the top stroke isn't clipped
        const qreal  B  = base;               // foot of the tab == separator line
        const qreal  rt = k_radius_top;
        const qreal  rb = k_radius_bottom;

        // Clockwise from the bottom-left baseline; bottom edge left open.
        QPainterPath path;
        path.moveTo(L, B);
        path.arcTo(L - rb,          B - 2 * rb, 2 * rb, 2 * rb, 270,  90);  // bottom-left flare (concave)
        path.lineTo(L + rb, T + rt);                                       // left side
        path.arcTo(L + rb,          T,          2 * rt, 2 * rt, 180, -90);  // top-left (convex)
        path.lineTo(R - rb - rt, T);                                       // top edge
        path.arcTo(R - rb - 2 * rt, T,          2 * rt, 2 * rt,  90, -90);  // top-right (convex)
        path.lineTo(R - rb, B - rb);                                       // right side
        path.arcTo(R - rb,          B - 2 * rb, 2 * rb, 2 * rb, 180,  90);  // bottom-right flare (concave) -> (R, B)

        // Fill the body white (close along the baseline), then stroke the open
        // outline so the bottom edge stays seamless with the pane.
        //
        // The fill is patched an extra 1px past B before painting: QPainter's
        // rasterizer treats a shape edge sitting exactly on an integer
        // coordinate as excluding that pixel row, so filling only out to B
        // left row B itself (the widget's very last pixel row, shared with
        // the foot-separator line drawn for the other tabs) uncovered —
        // showing the raw widget background through as a 1px gray seam
        // right under the active tab, contradicting the whole point of
        // leaving this edge open (a seamless join with the pane below).
        // The stroked outline still uses the un-patched `path`, so the
        // visible flare shape/line work is completely unaffected — only
        // the invisible interior fill is extended.
        QPainterPath fill = path;
        fill.closeSubpath();
        p.fillPath(fill, Qt::white);
        p.fillRect(QRectF(L, B, R - L, 1.0), Qt::white);
        p.strokePath(path, QPen(k_outline, 1.0));

        paint_label(p, i, k_text_active);
    }

    int hover_index_ = -1;

    static constexpr qreal k_radius_top    = 8.0;
    static constexpr qreal k_radius_bottom = 8.0;
    static constexpr int   k_min_height    = 34;

    inline static const QColor k_outline       {208, 208, 208};
    inline static const QColor k_hover         {241, 243, 244};
    inline static const QColor k_text_active   { 32,  32,  32};
    inline static const QColor k_text_inactive { 95,  99, 104};
};

// QTabWidget::setTabBar is protected, so installing a custom bar has to happen
// from inside a subclass.  Beyond that, this class also papers over a Qt
// layout quirk: QTabWidget sizes its tab bar to the bar's own sizeHint()
// (i.e. just wide enough for the tabs, like North/South tab bars without
// documentMode do) rather than stretching it to fill the row, even though
// individual tabs are left-aligned (setExpanding(false)) and there's a
// visible gap to their right above the pane. Because chrome_tab_bar's
// paintEvent draws its foot separator out to its own width() (see above),
// a too-narrow bar left that separator line stopping dead at the last tab
// instead of continuing across the rest of the strip to match the pane
// below it.
//
// An earlier version of this fix forced the tab bar itself wider by calling
// setGeometry() from a resize handler / event filter whenever Qt shrank it
// back. That fought Qt's own QTabBar layout code for control of the same
// geometry and turned out to be racy: depending on timing, QTabBar would
// sometimes respond by redistributing the extra width evenly across all
// tabs — exactly the "expanding" look setExpanding(false) is meant to
// prevent, and confirmed to reproduce inconsistently across runs with an
// identical tab set. Never touching tabBar()'s geometry avoids that fight
// entirely: we leave the bar at whatever (narrower) size Qt gives it, and
// simply paint the missing stretch of separator line ourselves, directly
// on the tab widget, in the untouched strip to the bar's right.
class chrome_tab_widget : public QTabWidget
{
public:
    explicit chrome_tab_widget(QWidget* parent = nullptr)
        : QTabWidget(parent)
    {
        setTabBar(new chrome_tab_bar(this));
    }

protected:
    void paintEvent(QPaintEvent* e) override
    {
        QTabWidget::paintEvent(e);

        // Continue the tab bar's own foot separator (see chrome_tab_bar::
        // paintEvent) across whatever width Qt left unclaimed to the bar's
        // right. k_outline here must stay in sync with chrome_tab_bar's
        // private k_outline of the same name/value. Skip entirely with no
        // songs open — chrome_tab_bar itself draws no separator in that
        // case (nothing to divide), so extending one here would leave a
        // stray line with no tab it's separating anything from.
        QTabBar* bar = tabBar();
        if (!bar || bar->count() == 0)
            return;
        const QRect br = bar->geometry();
        const int   from_x = br.right() + 1;
        if (from_x < width())
        {
            QPainter p(this);
            p.setPen(QPen(k_outline, 1.0));
            p.drawLine(QPointF(from_x, br.bottom()), QPointF(width(), br.bottom()));
        }
    }

private:
    inline static const QColor k_outline{208, 208, 208};
};

main_window::main_window(database& db, QWidget* parent)
    : QWidget(parent)
    , db_(db)
{
    // Force a white-paper background here too, matching the rest of
    // the app's print-look palette policy.
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::white);
    pal.setColor(QPalette::Base,   Qt::white);
    pal.setColor(QPalette::Text,   Qt::black);
    pal.setColor(QPalette::WindowText, Qt::black);
    setPalette(pal);
    setAutoFillBackground(true);

    // --- Layout ---
    // Outer: [side_panel] [main column]
    // Main column: [tab widget].  The sidebar toggle is a corner widget in the
    // tab bar (see below), so it no longer needs a row of its own.
    auto* outer = new QHBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    panel_ = new side_panel(this);
    outer->addWidget(panel_);

    auto* main_col = new QVBoxLayout;
    main_col->setContentsMargins(0, 0, 0, 0);
    main_col->setSpacing(0);
    outer->addLayout(main_col, /*stretch=*/1);

    // The tab widget.  Tabs are closable; close requests come back to
    // close_tab_at where we flush-save and drop the tab.  Tabs are
    // movable so users can reorder their workspace.
    tabs_ = new chrome_tab_widget(this);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    // documentMode is intentionally left OFF.  It would draw a tab-bar "base"
    // line, and Qt mis-places that line part-way up the tabs when the tab
    // widget has a top-left corner widget (our hamburger) — a light rule that
    // runs straight through the tab labels.  We don't need documentMode for the
    // look anyway: the flat, chart-like appearance comes entirely from the
    // custom chrome_tab_bar painting plus the borderless white pane stylesheet
    // below, both of which are unaffected by this flag.

    // The Chrome-style tab bar is installed by chrome_tab_widget's constructor
    // (QTabWidget::setTabBar is protected, so it can't be called from here).
    // That bar paints each tab itself: rounded top corners, outward-flaring
    // bottom corners on the current tab, and a foot separator that breaks under
    // the current tab so it reads as fused to the content.

    // The pane is just the white "printed page" the tabs sit on.  No border
    // here — the tab bar draws its own foot separator (with the gap under the
    // active tab), so a pane border would only double the line.  The default
    // close-button subcontrol is collapsed to nothing because we install our
    // own custom tab_close_button widget (below) on every tab.
    tabs_->setStyleSheet(
        "QTabWidget::pane {"
        "    border: none;"
        "    background: white;"
        "}"
        "QTabBar::close-button {"
        "    image: none;"        // hide the OS-provided close button image
        "    width: 0px;"         // collapse the default close-button slot
        "    height: 0px;"        // (our custom widget sits in its place)
        "}"
    );

    // Install a custom close button on every new tab.  We connect to
    // tabBar()'s tabBarClicked signal as a creation hook: Qt installs
    // our setTabButton call right after addTab returns, so we can't do
    // it from open_song directly (the index isn't stable until the tab
    // is fully inserted).  Instead we use QTabWidget::tabInserted via
    // a subclass — but since we can't subclass here, we re-install the
    // button immediately after addTab in open_song() instead (see below).
    connect(tabs_, &QTabWidget::tabCloseRequested,
            this, &main_window::close_tab_at);
    connect(tabs_, &QTabWidget::currentChanged,
        [this](int /*idx*/) {
            emit current_tab_changed(current_tab());
        });

    // Sidebar toggle: three thin lines, placed in the tab bar's top-left
    // corner so it sits level with the tab labels and reads like a leading
    // element of the tab strip rather than occupying its own row above it.
    hamburger_ = new QToolButton(this);
    hamburger_->setIcon(make_hamburger_icon());
    hamburger_->setIconSize(QSize(16, 12));
    hamburger_->setAutoRaise(true);         // flat until hovered
    hamburger_->setFocusPolicy(Qt::NoFocus);
    hamburger_->setCursor(Qt::PointingHandCursor);
    hamburger_->setToolTip(tr("Show songs and playlists"));
    connect(hamburger_, &QToolButton::clicked,
            panel_, &side_panel::toggle_animated);
    tabs_->setCornerWidget(hamburger_, Qt::TopLeftCorner);

    main_col->addWidget(tabs_, /*stretch=*/1);

    // Wire side-panel signals.  Songs open into tabs.  Playlist
    // double-click is currently a no-op (a slot is connected for
    // future use, just doesn't do anything yet).
    connect(panel_, &side_panel::song_double_clicked,
        [this](const QString& name) {
            open_song(name.toStdString());
        });
    connect(panel_, &side_panel::playlist_double_clicked,
        [](const QString& /*name*/) {
            // No-op for now.  Future: open all playlist songs as tabs.
        });

    // Add / delete from the side panel.  We forward to the same
    // confirm_delete_* / prompt_new_* methods the top-level menus
    // call so behavior is identical regardless of how the user got
    // here.  Keeping the prompts here (in main_window, not in
    // side_panel) means side_panel stays database-ignorant and
    // testable in isolation.
    connect(panel_, &side_panel::add_song_requested,
            this, &main_window::prompt_new_song);
    connect(panel_, &side_panel::add_playlist_requested,
            this, &main_window::prompt_new_playlist);
    connect(panel_, &side_panel::delete_song_requested,
        [this](const QString& name) {
            confirm_delete_song(name.toStdString());
        });
    connect(panel_, &side_panel::delete_playlist_requested,
        [this](const QString& name) {
            confirm_delete_playlist(name.toStdString());
        });

    // Sort-key change: persist the new key to the database's metadata, then
    // refresh the list so it re-sorts by it.  Persisting here (rather than
    // only on quit) means the choice survives a crash and is already durable
    // when a file is saved.
    connect(panel_, &side_panel::sort_key_changed,
        [this](const QString& token) {
            try
            {
                db_.song_list_sort(token.toStdString());
            }
            catch (const std::exception&)
            {
                // Persisting the preference failed; still re-sort the visible
                // list so the user's pick takes effect this session.
            }
            refresh_lists();
        });

    // Sort-direction toggle: persist and re-sort, same shape as the key.
    connect(panel_, &side_panel::sort_direction_changed,
        [this](bool descending) {
            try
            {
                db_.song_list_sort_descending(descending);
            }
            catch (const std::exception&)
            {
            }
            refresh_lists();
        });

    // Populate lists on construction.  The database is already open
    // at this point (app constructs it before us), so these queries
    // return the current names.
    refresh_lists();

    // Construct the modal overlay last so it sits on top of every
    // sibling widget added by the layout.  It's hidden by default;
    // prompt/confirm/message methods show it on demand.
    overlay_ = new modal_overlay(this);
}

namespace
{
// Case-insensitive compare of two text values.  Used for every field that's
// naturally a string: name, performer, album, notes, and the authors list
// once joined into one line.
int ci_compare(const std::string& a, const std::string& b)
{
    return QString::fromStdString(a)
        .compare(QString::fromStdString(b), Qt::CaseInsensitive);
}

// metadata::authors is a vector<string>, one entry per author.  Both the
// list column and the sort want it as a single line, joined with the same
// separator the database's CSV column uses, so this reads the same way a
// user who typed "Lennon, McCartney" into the authors field would expect.
std::string join_authors(const std::vector<std::string>& authors)
{
    std::string joined;
    for (std::size_t i = 0; i < authors.size(); ++i)
    {
        joined += authors[i];
        if (i + 1 < authors.size())
            joined += ", ";
    }
    return joined;
}

// Local-time formatting for the two chrono timestamps, matching
// song_info_panel's fmt_timestamp so the Info pane and the song list agree
// on what "Created"/"Modified" show for the same song.  Timestamps are
// stored (and compared, below) as UTC, but must always be *displayed* in
// local time — QDateTime::fromMSecsSinceEpoch does that conversion.
QString fmt_local(std::chrono::sys_time<std::chrono::milliseconds> t)
{
    const qint64 ms = static_cast<qint64>(t.time_since_epoch().count());
    return QDateTime::fromMSecsSinceEpoch(ms).toString("yyyy-MM-dd HH:mm");
}

// release_date is a calendar date (std::chrono::sys_days) with no time of
// day or zone, so — unlike created/modified — it needs no UTC-to-local
// conversion; the same date displays everywhere.
QString fmt_date(std::chrono::sys_days d)
{
    const std::chrono::year_month_day ymd{d};
    return QDate(static_cast<int>(ymd.year()),
                 static_cast<int>(static_cast<unsigned>(ymd.month())),
                 static_cast<int>(static_cast<unsigned>(ymd.day())))
        .toString("yyyy-MM-dd");
}

// Three-way compare on the chosen field, breaking ties by name so equal keys
// group predictably.  Returns <0, 0, >0; the caller applies direction.
// Now that song_summary carries typed metadata (see database.hpp) rather
// than pre-stringified columns, each field gets its natural comparison:
// timestamps and dates compare numerically/chronologically instead of as
// text, and only the genuinely textual fields go through ci_compare.  Keep
// these tokens in step with k_sort_options in side_panel.cpp.
int compare_summaries(const song_summary& a, const song_summary& b,
                      const std::string& token)
{
    int primary = 0;
    if (token == "authors")
        primary = ci_compare(join_authors(a.meta.authors), join_authors(b.meta.authors));
    else if (token == "performer")
        primary = ci_compare(a.meta.original_performer, b.meta.original_performer);
    else if (token == "album")
        primary = ci_compare(a.meta.original_album, b.meta.original_album);
    else if (token == "notes")
        primary = ci_compare(a.meta.notes, b.meta.notes);
    else if (token == "release_date")
    {
        const auto& ra = a.meta.original_album_release_date;
        const auto& rb = b.meta.original_album_release_date;
        // Songs with no release date sort before any dated song — a
        // consistent, predictable spot rather than wherever an empty
        // string happened to fall under text comparison.
        if (ra == rb)
            primary = 0;
        else if (!ra)
            primary = -1;
        else if (!rb)
            primary = 1;
        else
            primary = (*ra < *rb) ? -1 : 1;
    }
    else if (token == "created")
    {
        primary = (a.meta.creation_time == b.meta.creation_time) ? 0
                : (a.meta.creation_time < b.meta.creation_time ? -1 : 1);
    }
    else if (token == "modified")
    {
        primary = (a.meta.modification_time == b.meta.modification_time) ? 0
                : (a.meta.modification_time < b.meta.modification_time ? -1 : 1);
    }
    // else: "name" and any unrecognised token fall through with primary==0,
    // so the name tie-break below ends up being the actual sort key.

    if (primary != 0)
        return primary;
    return ci_compare(a.name, b.name);
}

// The value shown to the right of the name for the current sort key.  Empty
// for "name" (it would just repeat the name) and for an unset release_date.
std::string display_value(const song_summary& s, const std::string& token)
{
    if (token == "authors")      return join_authors(s.meta.authors);
    if (token == "performer")    return s.meta.original_performer;
    if (token == "album")        return s.meta.original_album;
    if (token == "notes")        return s.meta.notes;
    if (token == "release_date")
        return s.meta.original_album_release_date
             ? fmt_date(*s.meta.original_album_release_date).toStdString()
             : std::string{};
    if (token == "created")      return fmt_local(s.meta.creation_time).toStdString();
    if (token == "modified")     return fmt_local(s.meta.modification_time).toStdString();
    return {};   // "name" and any unrecognised token
}

// Sort summaries by key and direction, and pair each name with its display
// value for the second column.
std::vector<std::pair<std::string, std::string>>
build_song_rows(std::vector<song_summary> rows, const std::string& token, bool descending)
{
    std::sort(rows.begin(), rows.end(),
        [&](const song_summary& a, const song_summary& b) {
            const int c = compare_summaries(a, b, token);
            return descending ? c > 0 : c < 0;
        });

    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(rows.size());
    for (auto& r : rows)
        out.emplace_back(r.name, display_value(r, token));
    return out;
}
} // namespace

void main_window::refresh_lists()
{
    try
    {
        // The sort key and direction live in the database (UI preferences in
        // the metadata table); the sort itself happens here.  Reflect both in
        // the panel, then fetch the unsorted summaries and order them.
        const std::string key  = db_.song_list_sort();
        const bool        desc = db_.song_list_sort_descending();
        panel_->set_sort_key(QString::fromStdString(key));
        panel_->set_sort_direction(desc);
        panel_->set_song_rows(build_song_rows(db_.song_summaries(), key, desc));
        panel_->set_playlist_names(db_.select_playlist_names());
    }
    catch (const std::exception&)
    {
        // If the DB is broken we don't want to crash the UI.  An
        // empty list is the right fallback; the user will notice
        // their songs are missing and we'll surface a real error
        // through the status bar later.
    }
}

void main_window::open_song(const std::string& name)
{
    // Already open?  Focus the existing tab.  Tabs are uniquely
    // keyed by song name because the database enforces unique song
    // names — opening "Yesterday" twice would either mean two tabs
    // editing the same row (bad) or just confusing the user.
    const int existing = find_tab_by_name(name);
    if (existing >= 0)
    {
        tabs_->setCurrentIndex(existing);
        return;
    }

    // Load from the database.  select_song throws if the name isn't
    // found; that shouldn't happen here because the only path to
    // this method is a double-click on a name we just listed, but
    // be defensive.
    std::unique_ptr<model::song> song;
    std::optional<song_id> id;
    try
    {
        auto loaded = db_.select_song(name);
        id = loaded.id;
        song = std::make_unique<model::song>(std::move(loaded.value));
    }
    catch (const std::exception&)
    {
        return;
    }

    add_tab(std::move(song), id);
}

void main_window::add_tab(std::unique_ptr<model::song> song,
                          std::optional<song_id> id)
{
    auto* tab = new song_tab(std::move(song), id, db_, this);
    // Give the body widget access to the overlay so its prompts
    // (Voltas, Custom beats) use the Wayland-safe in-widget overlay
    // rather than QInputDialog.
    tab->widget()->body()->set_overlay(overlay_);
    const int idx = tabs_->addTab(tab, tab->tab_name());

    // Install our custom close button in place of the OS-provided one.
    // Qt's QTabBar::setTabButton places an arbitrary widget in the
    // RightSide button slot; wiring its clicked() to tabCloseRequested
    // reuses the same close path as the default button.
    auto* close_btn = new tab_close_button(tabs_->tabBar());
    connect(close_btn, &QAbstractButton::clicked, this, [this, close_btn]() {
        QTabBar* bar = tabs_->tabBar();
        for (int i = 0; i < bar->count(); ++i)
        {
            if (bar->tabButton(i, QTabBar::RightSide) == close_btn)
            {
                emit tabs_->tabCloseRequested(i);
                return;
            }
        }
    });
    tabs_->tabBar()->setTabButton(idx, QTabBar::RightSide, close_btn);

    // Keep the tab label and the side-panel song list in step when the song
    // is renamed from the chart.  The tab text comes from the in-memory model
    // (tab_name()); the list is re-read from the database, which song_tab has
    // already flushed the rename into by the time this fires.  Look the tab's
    // index up at signal time — it can move as other tabs open and close.
    connect(tab, &song_tab::renamed, this, [this, tab]() {
        const int at = tabs_->indexOf(tab);
        if (at >= 0)
            tabs_->setTabText(at, tab->tab_name());
        refresh_lists();
    });

    tabs_->setCurrentIndex(idx);
}

void main_window::restore_open_tabs()
{
    // Pull the songs the database recorded as open last time and reopen a tab
    // for each.  A malformed/garbled list must never block the app from
    // opening, so failures here are swallowed — worst case the user starts
    // with no tabs.
    std::vector<stored_song> songs;
    try
    {
        songs = db_.last_open_songs();
    }
    catch (const std::exception&)
    {
        return;
    }

    for (auto& ss : songs)
        add_tab(std::make_unique<model::song>(std::move(ss.value)), ss.id);

    // add_tab focuses each tab as it's added; leave the first one focused.
    if (tabs_->count() > 0)
        tabs_->setCurrentIndex(0);

    emit current_tab_changed(current_tab());
}

void main_window::persist_open_tabs()
{
    // Record the row id of every open tab, in tab order.  Tabs for songs that
    // have never been saved have no id yet and are simply omitted.
    std::vector<song_id> ids;
    for (int i = 0; i < tabs_->count(); ++i)
        if (auto* tab = qobject_cast<song_tab*>(tabs_->widget(i)))
            if (auto id = tab->song_identity())
                ids.push_back(*id);
    db_.last_open_songs(ids);
}

void main_window::close_tab_at(int index)
{
    if (index < 0 || index >= tabs_->count())
        return;
    QWidget* w = tabs_->widget(index);
    auto* tab = qobject_cast<song_tab*>(w);
    if (tab)
        tab->flush_save();   // belt and suspenders — destructor flushes too,
                              // but flushing here means the side-panel
                              // refresh below sees the post-save state
    tabs_->removeTab(index);
    if (tab)
        tab->deleteLater();
    refresh_lists();
    emit current_tab_changed(current_tab());
}

void main_window::flush_all_tabs()
{
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (auto* tab = qobject_cast<song_tab*>(tabs_->widget(i)))
            tab->flush_save();
    }
}

namespace
{
// One source of truth for the file type so the Save and Open dialogs agree.
const char* const kFileFilter = "Nashville charts (*.nashv);;All files (*)";
const char* const kFileSuffix = "nashv";
}

void main_window::save()
{
    if (db_.in_memory())
    {
        // Untitled: there's no path yet, so a plain Save has to become
        // Save As and ask for one.
        save_as();
    }
    else
    {
        // Already file-backed and continuously auto-saving; an explicit
        // Save just makes that durable right now by flushing every tab's
        // in-flight edits into the live database.
        flush_all_tabs();
    }
}

bool main_window::save_as()
{
    // Seed the dialog: Documents/Untitled for a fresh session, or the existing
    // file's own path when relocating a file-backed database.
    QString start;
    if (db_.in_memory())
    {
        const QString docs =
            QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
        start = QDir(docs).filePath(QString::fromUtf8("Untitled.") +
                                    QString::fromUtf8(kFileSuffix));
    }
    else
    {
        start = QString::fromStdString(db_.file_name().string());
    }

    QString chosen = QFileDialog::getSaveFileName(
        this, tr("Save As"), start, QString::fromUtf8(kFileFilter));
    if (chosen.isEmpty())
        return false;  // user cancelled — stay exactly as we were
    if (QFileInfo(chosen).suffix().isEmpty())
        chosen += QString::fromUtf8(".") + QString::fromUtf8(kFileSuffix);

    // Critical ordering: the database snapshots itself as-is, so flush every
    // open tab's debounced edits into it FIRST, or the user's most recent few
    // seconds of typing won't make it into the file they just named.
    flush_all_tabs();

    try
    {
        db_.move_to_file(std::filesystem::path(chosen.toStdString()));
    }
    catch (const std::exception& e)
    {
        // move_to_file is all-or-nothing: on any failure we're still on the
        // intact in-memory database and no file was damaged, so it's safe to
        // simply report and return — the user can retry elsewhere.
        overlay_->message(tr("Could not save"), QString::fromUtf8(e.what()));
        return false;
    }

    update_window_title();
    return true;
}

bool main_window::has_unsaved_scratch() const
{
    return db_.in_memory() &&
           !(db_.select_song_names().empty() && db_.select_playlist_names().empty());
}

// ---------------------------------------------------------------------------
// eventFilter — quit-confirmation guard
// ---------------------------------------------------------------------------
// See the header comment for why this lives here (main_window owns the
// save/discard business logic) despite watching an object (the
// top-level QMainWindow) it doesn't itself own.
bool main_window::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Close && !quit_confirmed_)
    {
        // Flush first so a song created (or edited) in the last second
        // — before its debounced autosave has fired, and so possibly
        // not in the database yet at all — is accounted for below.
        // Without this, a brand-new song could read as "nothing to
        // lose" simply because it hasn't been inserted yet.
        flush_all_tabs();

        if (has_unsaved_scratch())
        {
            event->ignore();
            overlay_->confirm(
                tr("Quit Nashville"),
                tr("Your work isn't saved to a file yet. "
                   "Save it before quitting?"),
                [this, watched](bool save_first) {
                    if (save_first && !save_as())
                        return;  // cancelled the save dialog, or it
                                 // failed — stay open either way
                    quit_confirmed_ = true;
                    if (auto* w = qobject_cast<QWidget*>(watched))
                        w->close();
                });
            return true;  // consumed: don't let the close proceed yet
        }
    }
    return QWidget::eventFilter(watched, event);
}

void main_window::prompt_open()
{
    // Decide whether the current workspace needs saving before we replace it.
    // Only an *in-memory* session with content is at risk: a file-backed
    // database is already durable (we flush it below), and an empty scratch
    // session has nothing to lose.
    const bool unsaved_scratch = has_unsaved_scratch();

    if (unsaved_scratch)
    {
        overlay_->confirm(
            tr("Open another file"),
            tr("Your current workspace isn't saved to a file yet. "
               "Save it before opening another?"),
            [this](bool save_first) {
                if (save_first)
                {
                    // Only proceed to the open dialog if the save actually
                    // happened; if the user cancelled or it failed, stop so
                    // the unsaved work isn't discarded.
                    if (save_as())
                        open_replacing_current();
                }
                else
                {
                    open_replacing_current();  // deliberately discard scratch
                }
            });
    }
    else
    {
        // File-backed: make the continuous auto-save durable before we drop
        // the connection by opening another file.
        if (!db_.in_memory())
            flush_all_tabs();
        open_replacing_current();
    }
}

void main_window::open_replacing_current()
{
    const QString start = db_.in_memory()
        ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
        : QString::fromStdString(db_.file_name().parent_path().string());

    const QString chosen = QFileDialog::getOpenFileName(
        this, tr("Open"), start, QString::fromUtf8(kFileFilter));
    if (chosen.isEmpty())
        return;  // cancelled — keep the current workspace

    // Record the current workspace's open tabs into the current (about-to-be-
    // replaced) database, so reopening this file later restores them.  The
    // caller (prompt_open) has already flushed/saved, so ids exist.
    persist_open_tabs();

    try
    {
        db_.open_file(std::filesystem::path(chosen.toStdString()));
    }
    catch (const std::exception& e)
    {
        // open_file is all-or-nothing: on failure we're still on the current
        // database with its tabs intact, so just report and stay put.
        overlay_->message(tr("Could not open"), QString::fromUtf8(e.what()));
        return;
    }

    // The swap succeeded: the open tabs reference the previous database's rows,
    // so drop them WITHOUT saving (we already saved or discarded above; saving
    // now would write stale ids into the newly-opened database).  No event loop
    // runs between open_file and here, so no debounced save can fire in the gap.
    close_all_tabs_without_saving();
    refresh_lists();
    update_window_title();

    // Reopen whatever tabs the newly-opened file had open when it was last
    // closed.
    restore_open_tabs();
}

void main_window::update_window_title()
{
    const QString name = db_.in_memory()
        ? tr("Untitled")
        : QString::fromStdString(db_.file_name().filename().string());
    if (QWidget* top = window())
        top->setWindowTitle(name + tr(" — Nashville"));
}

void main_window::apply_font_scale_to_all_tabs(qreal scale)
{
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (auto* tab = qobject_cast<song_tab*>(tabs_->widget(i)))
            tab->widget()->body()->apply_font_scale(scale);
    }
}

void main_window::close_all_tabs_without_saving()
{
    // Walk in reverse because removeTab shifts higher indices down,
    // and so we delete in a predictable order.  For each tab,
    // suppress its destructor's flush_save before removing it from
    // the tab widget.  removeTab does NOT delete the contained
    // widget — we own its lifetime and delete it explicitly here.
    // (Qt would otherwise delete it via the parent-child cascade
    // when this widget is destroyed, but that runs at a moment we
    // don't control, possibly after the database is gone.)
    while (tabs_->count() > 0)
    {
        const int last = tabs_->count() - 1;
        QWidget* w = tabs_->widget(last);
        tabs_->removeTab(last);
        if (auto* tab = qobject_cast<song_tab*>(w))
        {
            tab->suppress_destructor_save();
            delete tab;
        }
        else
        {
            delete w;
        }
    }
}

int main_window::find_tab_by_name(const std::string& name) const
{
    for (int i = 0; i < tabs_->count(); ++i)
    {
        if (auto* tab = qobject_cast<song_tab*>(tabs_->widget(i)))
        {
            if (tab->song_name() == name)
                return i;
        }
    }
    return -1;
}

song_tab* main_window::current_tab() const
{
    if (tabs_->count() == 0)
        return nullptr;
    return qobject_cast<song_tab*>(tabs_->currentWidget());
}

std::string main_window::selected_song_name() const
{
    return panel_->selected_song_name().toStdString();
}

std::string main_window::selected_playlist_name() const
{
    return panel_->selected_playlist_name().toStdString();
}

// ---------------------------------------------------------------------------
// New-song / new-playlist / delete-song / delete-playlist flows
// ---------------------------------------------------------------------------
// These methods used to be synchronous: they would pop a QDialog,
// block until exec returned, read the result, then continue.  With
// the move to modal_overlay (which is async — the callback fires
// after the user dismisses the prompt) the continuation has to live
// inside a lambda.  The structure is the same; the dialog wrappers
// just spell as overlay->prompt_text(..., [this](result) { ... })
// instead of `auto result = prompt(...); ...`.
//
// All four methods reach for the database, refresh the side panel,
// and (for the new-song case) open the freshly-created song in a
// tab.  Validation against duplicates happens in is_usable_new_name,
// which is itself async now because it may need to show a warning
// overlay if the name collides.

// Helper: check whether `candidate` is a usable new name.  If empty,
// silently rejects (caller treats as cancel).  If duplicate of an
// existing name in `existing`, shows a warning overlay and calls
// `on_done(false)` after the user dismisses it; otherwise calls
// `on_done(true)` immediately.  `kind` is "song" / "playlist" for
// the warning message wording.
static void is_usable_new_name(modal_overlay* overlay,
                               const QString& candidate,
                               const std::vector<std::string>& existing,
                               const QString& kind,
                               std::function<void(bool)> on_done)
{
    const QString trimmed = candidate.trimmed();
    if (trimmed.isEmpty())
    {
        on_done(false);
        return;
    }
    for (const auto& e : existing)
    {
        if (QString::fromStdString(e) == trimmed)
        {
            // Show the warning, then call back with false once
            // the user dismisses.  Capturing on_done by value
            // (copy) means the lambda owns its own copy of the
            // closure; the calling stack frame can return before
            // the user clicks OK.
            overlay->message(
                QObject::tr("Name already in use"),
                QObject::tr("A %1 named \"%2\" already exists. "
                            "Please choose a different name.")
                    .arg(kind, trimmed),
                [on_done]() { on_done(false); });
            return;
        }
    }
    on_done(true);
}

void main_window::prompt_new_song()
{
    overlay_->prompt_text(
        tr("New song"),
        tr("Song name:"),
        [this](std::optional<QString> name) {
            if (!name) return;  // user cancelled
            // Validate.  is_usable_new_name is async (it may show
            // a warning overlay on duplicate names), so the
            // database write has to happen inside its callback.
            is_usable_new_name(overlay_, *name,
                db_.select_song_names(), tr("song"),
                [this, name](bool ok) {
                    if (!ok) return;
                    try
                    {
                        model::song s(name->toStdString());
                        db_.insert_song(s);
                    }
                    catch (const std::exception& e)
                    {
                        overlay_->message(tr("Could not create song"),
                                          QString::fromUtf8(e.what()));
                        return;
                    }
                    refresh_lists();
                    open_song(name->toStdString());
                });
        });
}

void main_window::prompt_new_playlist()
{
    overlay_->prompt_text(
        tr("New playlist"),
        tr("Playlist name:"),
        [this](std::optional<QString> name) {
            if (!name) return;
            is_usable_new_name(overlay_, *name,
                db_.select_playlist_names(), tr("playlist"),
                [this, name](bool ok) {
                    if (!ok) return;
                    try
                    {
                        model::playlist pl(name->toStdString());
                        db_.insert_playlist(pl);
                    }
                    catch (const std::exception& e)
                    {
                        overlay_->message(tr("Could not create playlist"),
                                          QString::fromUtf8(e.what()));
                        return;
                    }
                    refresh_lists();
                });
        });
}

void main_window::confirm_delete_song(const std::string& name)
{
    if (name.empty()) return;

    const QString qname = QString::fromStdString(name);
    overlay_->confirm(
        tr("Delete song"),
        tr("Delete the song \"%1\"?\n\nThis cannot be undone.").arg(qname),
        [this, name](bool yes) {
            if (!yes) return;

            // If the song has an open tab, the tab's auto-save would
            // re-insert the row we're about to delete.  We delete
            // the DB row first, then close the tab; the tab's
            // destructor will try to flush_save but we re-delete
            // after via a queued invoke to cancel that race.  See
            // the old confirm_delete_song for the longer comment.
            const int existing = find_tab_by_name(name);
            try
            {
                db_.remove_song(name);
            }
            catch (const std::exception& e)
            {
                overlay_->message(tr("Could not delete song"),
                                  QString::fromUtf8(e.what()));
                return;
            }
            if (existing >= 0)
            {
                QWidget* w = tabs_->widget(existing);
                tabs_->removeTab(existing);
                if (auto* tab = qobject_cast<song_tab*>(w))
                {
                    // Suppress the destructor's flush_save so the
                    // tab can't re-insert the row we just deleted.
                    // This is the simpler half of the cleanup;
                    // before this method gained suppress_destructor_
                    // save we had to queue a follow-up re-delete on
                    // the event loop.
                    tab->suppress_destructor_save();
                    tab->deleteLater();
                }
            }
            refresh_lists();
            emit current_tab_changed(current_tab());
        });
}

void main_window::confirm_delete_playlist(const std::string& name)
{
    if (name.empty()) return;

    const QString qname = QString::fromStdString(name);
    overlay_->confirm(
        tr("Delete playlist"),
        tr("Delete the playlist \"%1\"?\n\nThis cannot be undone.").arg(qname),
        [this, name](bool yes) {
            if (!yes) return;
            try
            {
                db_.remove_playlist(name);
            }
            catch (const std::exception& e)
            {
                overlay_->message(tr("Could not delete playlist"),
                                  QString::fromUtf8(e.what()));
                return;
            }
            refresh_lists();
        });
}

} // namespace nashville::view
