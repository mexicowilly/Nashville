#include "margin_renderer.hpp"
#include <QFontMetricsF>
#include <QFont>
#include <QFontDatabase>
#include <cmath>

namespace nashville::view
{

static constexpr const char* k_flat  = "\u266D"; // ♭
static constexpr const char* k_sharp = "\u266F"; // ♯

// Color used for placeholder text on empty fields (medium gray).
static const QColor k_placeholder_color(150, 150, 150);

void margin_renderer::paint(QPainter& painter,
                            const QRectF& margin_rect,
                            const model::song& song,
                            margin_layout* out_layout,
                            qreal font_scale)
{
    painter.save();

    QFont base_font("Georgia");
    base_font.setPointSizeF(11.0 * font_scale);
    QFont key_font("Georgia");
    key_font.setBold(true);
    key_font.setPointSizeF(13.5 * font_scale);
    QFontMetricsF base_fm(base_font);

    // Load Bravura for the tempo note glyph so it matches the rhythm row.
    int bravura_id = QFontDatabase::addApplicationFont(":/fonts/Bravura.otf");
    QString music_family = (bravura_id != -1)
                           ? QFontDatabase::applicationFontFamilies(bravura_id).first()
                           : base_font.family();
    QFont music_font(music_family);
    music_font.setPointSizeF(11.0 * font_scale);

    qreal cx = margin_rect.center().x();
    qreal y  = margin_rect.top() + k_top_padding;

    // ----------------------------------------------------------------
    // 1. Key — letter (+ accidental) (+ free-form suffix) inside a circle
    //
    // The user can store any string in the key — "A", "Bb", "A min",
    // "F# Major", "C dorian", … — so we render whatever's there.  The
    // circle's diameter is capped at the margin width so the chart area
    // isn't pushed around by long keys; if the text would overflow, the
    // font is scaled down until it fits.
    // ----------------------------------------------------------------
    QString key_letter, key_accidental, key_suffix;
    const bool key_empty = song.key().empty();
    parse_key(song.key(), key_letter, key_accidental, key_suffix);
    QString key_display = key_letter + key_accidental + key_suffix;

    // Maximum circle radius so the circle (and any inset text padding) fit
    // inside the margin column with a small gutter to either side.
    constexpr qreal k_margin_gutter = 4.0;
    qreal max_radius = std::max<qreal>(
        12.0,
        margin_rect.width() / 2.0 - k_margin_gutter);

    // Find a font size at which the text fits comfortably inside that
    // capped circle.  Start at the configured key font size and shrink in
    // 1pt steps; accept a size when the natural circle radius for the text
    // is within the cap, or when we hit the floor.
    QFont scaled_key_font = key_font;
    constexpr qreal k_min_key_pt = 8.0;
    qreal try_pt = key_font.pointSizeF();
    qreal key_text_w = 0.0;
    qreal key_text_h = 0.0;
    qreal natural_r  = 0.0;
    while (true)
    {
        scaled_key_font.setPointSizeF(try_pt);
        QFontMetricsF fm(scaled_key_font);
        key_text_w = fm.horizontalAdvance(key_display);
        key_text_h = fm.height();
        natural_r  = std::max(key_text_w, key_text_h) / 2.0 + k_circle_padding;
        if (natural_r <= max_radius || try_pt <= k_min_key_pt)
            break;
        try_pt -= 1.0;
    }

    QFontMetricsF scaled_key_fm(scaled_key_font);
    qreal circle_r = std::min(natural_r, max_radius);

    QPointF circle_center(cx, y + circle_r);

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(Qt::black, 1.5));
    painter.drawEllipse(circle_center, circle_r, circle_r);

    qreal key_x        = circle_center.x() - key_text_w / 2.0;
    qreal key_baseline = circle_center.y()
                        + (scaled_key_fm.ascent() - scaled_key_fm.descent()) / 2.0;

    // Empty key renders as gray "?" — placeholder for an unset field.
    painter.setFont(scaled_key_font);
    painter.setPen(QPen(key_empty ? k_placeholder_color : Qt::black, 1.0));

    // Render the three parts (letter, accidental, suffix) at distinct
    // positions instead of one drawText call, so the accidental's
    // baseline can be lifted to align its visual ink centre with the
    // letter's.  Common text fonts (Georgia included) draw ♯ / ♭ with
    // their ink centre near the x-height — well below the cap-height
    // centre of a capital letter — so a naïve same-baseline draw makes
    // "F#" look like "F" sitting on top of a dropped "#".  Aligning the
    // tight-bounding-rect centres puts the accidental at the same
    // visual elevation as the letter.
    qreal cursor_x = key_x;

    if (!key_letter.isEmpty())
    {
        painter.drawText(QPointF(cursor_x, key_baseline), key_letter);
        cursor_x += scaled_key_fm.horizontalAdvance(key_letter);
    }

    if (!key_accidental.isEmpty())
    {
        QRectF letter_tbr = key_letter.isEmpty()
                            ? QRectF()
                            : scaled_key_fm.tightBoundingRect(key_letter);
        QRectF acc_tbr    = scaled_key_fm.tightBoundingRect(key_accidental);
        qreal letter_centre = letter_tbr.isEmpty()
                              ? 0.0
                              : (letter_tbr.top() + letter_tbr.bottom()) / 2.0;
        qreal acc_centre    = (acc_tbr.top() + acc_tbr.bottom()) / 2.0;
        qreal acc_baseline  = key_baseline + letter_centre - acc_centre;
        painter.drawText(QPointF(cursor_x, acc_baseline), key_accidental);
        cursor_x += scaled_key_fm.horizontalAdvance(key_accidental);
    }

    if (!key_suffix.isEmpty())
        painter.drawText(QPointF(cursor_x, key_baseline), key_suffix);

    if (out_layout)
    {
        out_layout->key_rect = QRectF(circle_center.x() - circle_r,
                                      circle_center.y() - circle_r,
                                      circle_r * 2.0,
                                      circle_r * 2.0);
    }

    y += circle_r * 2.0 + k_element_spacing;

    // ----------------------------------------------------------------
    // 2. Time signature — stacked numerals, centered
    // ----------------------------------------------------------------
    const auto& ts = song.time_sig();
    QString count_str = QString::number(ts.count());
    QString kind_str  = QString::number(static_cast<int>(ts.kind()));

    painter.setFont(base_font);
    painter.setPen(QPen(Qt::black, 1.0));
    qreal ts_num_w = std::max(base_fm.horizontalAdvance(count_str),
                              base_fm.horizontalAdvance(kind_str));

    qreal count_x = cx - base_fm.horizontalAdvance(count_str) / 2.0;
    qreal kind_x  = cx - base_fm.horizontalAdvance(kind_str)  / 2.0;

    qreal row_h    = base_fm.ascent() + base_fm.descent();  // full glyph height
    qreal sep      = 1.0;                                  // gap below glyph and above denominator
    qreal sep_left = cx - ts_num_w / 2.0;

    qreal ts_top = y;
    painter.drawText(QPointF(count_x, y + base_fm.ascent()), count_str);

    // Separator line — centred, 1px thick, with breathing room above and below
    qreal line_y = std::round(y + row_h + sep);
    painter.fillRect(QRectF(sep_left, line_y, ts_num_w, 1.0), Qt::black);

    painter.drawText(QPointF(kind_x, line_y + sep + base_fm.ascent()), kind_str);

    qreal ts_bottom = line_y + sep + row_h;
    if (out_layout)
    {
        // Generous horizontal hit-target around the stacked numerals.
        qreal pad_h = 8.0;
        out_layout->time_sig_rect = QRectF(sep_left - pad_h,
                                           ts_top,
                                           ts_num_w + pad_h * 2.0,
                                           ts_bottom - ts_top);
    }

    y += row_h + sep + 1.0 + sep + row_h + k_element_spacing;

    // ----------------------------------------------------------------
    // 3. Tempo — note glyph (Bravura) + " = " + BPM (Georgia), centered
    // ----------------------------------------------------------------
    auto [bpm, beat_unit] = song.tempo();
    QString    glyph_str = tempo_glyph(beat_unit);
    const bool dotted    = tempo_is_dotted(beat_unit);
    QString    text_str  = " = " + QString::number(bpm);

    // Scale Bravura from a single *reference* glyph rather than from the
    // glyph actually being drawn.  SMuFL metronome notes have intrinsic
    // relative sizes — a whole note is a short wide oval with no stem, a
    // quarter is tall and thin — so sizing each one individually to the same
    // ink height blows the shorter glyphs up by several times (a bare whole
    // note is ~250 units against the quarter's ~830, i.e. 3.3x too big).
    // One shared scale factor keeps the set in correct relative proportion,
    // with the quarter note's ink matching Georgia's cap height.
    QFont scaled_music = music_font;
    {
        QFontMetricsF mfm(music_font);
        QRectF ref_tbr = mfm.tightBoundingRect(QString(QChar(k_tempo_ref_glyph)));
        qreal target_h = base_fm.ascent();  // match Georgia cap height
        if (ref_tbr.height() > 0.0)
            scaled_music.setPointSizeF(music_font.pointSizeF() * (target_h / ref_tbr.height()));
    }
    QFontMetricsF scaled_mfm(scaled_music);

    // Compensate for any negative left bearing in the Bravura glyph.
    QRectF gtbr = scaled_mfm.tightBoundingRect(glyph_str);

    // Augmentation dot for dotted beat units, painted as a filled circle to
    // the right of the note (matching how chord_renderer dots rests in the
    // rhythm row).  Sized off the cap height so it tracks font_scale.
    const qreal dot_r    = dotted ? std::max(1.0, base_fm.ascent() * 0.075) : 0.0;
    const qreal dot_gap  = dotted ? dot_r * 1.0 : 0.0;
    const qreal dot_span = dotted ? (dot_gap + dot_r * 2.0) : 0.0;

    qreal glyph_w = scaled_mfm.horizontalAdvance(glyph_str) + dot_span;
    qreal text_w  = base_fm.horizontalAdvance(text_str);
    qreal total_w = glyph_w + text_w;
    qreal start_x = cx - total_w / 2.0;
    qreal baseline_y = y + base_fm.ascent();

    qreal glyph_draw_x = start_x - std::min(0.0, gtbr.left());

    // Vertical alignment.  Bravura centres its noteheads on the baseline, so
    // drawing the glyph on the text baseline leaves the head's centre a half
    // x-height below the ink of "= NNN" — the number reads as floating above
    // the note.  Engraving convention puts the notehead level with the middle
    // of the equals sign, so lift the glyph by the difference between the two
    // ink centres.  Both are measured rather than assumed, so the alignment
    // holds across font_scale and any font substitution.
    qreal head_centre = 0.0;
    {
        QRectF head_tbr = scaled_mfm.tightBoundingRect(QString(QChar(u'\uE0A4')));
        if (head_tbr.height() > 0.0)
            head_centre = (head_tbr.top() + head_tbr.bottom()) / 2.0;
    }
    qreal eq_centre = 0.0;
    {
        QRectF eq_tbr = base_fm.tightBoundingRect("=");
        if (eq_tbr.height() > 0.0)
            eq_centre = (eq_tbr.top() + eq_tbr.bottom()) / 2.0;  // negative: above baseline
    }
    const qreal glyph_baseline = baseline_y + eq_centre - head_centre;

    painter.setFont(scaled_music);
    painter.setPen(QPen(Qt::black, 1.0));
    painter.drawText(QPointF(glyph_draw_x, glyph_baseline), glyph_str);

    if (dotted)
    {
        // Dot rides at the notehead's centre, which is where the equals sign
        // now sits too.
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::black);
        painter.drawEllipse(
            QPointF(glyph_draw_x + gtbr.right() + dot_gap + dot_r,
                    glyph_baseline + head_centre),
            dot_r, dot_r);
        painter.restore();
    }

    painter.setFont(base_font);
    painter.setPen(QPen(Qt::black, 1.0));
    painter.drawText(QPointF(start_x + glyph_w, baseline_y), text_str);

    if (out_layout)
    {
        qreal pad_h = 6.0;
        qreal pad_v = 2.0;

        // The glyph now sits above the text baseline's cap line, so grow the
        // hit rects upward to whichever of the two starts higher.
        qreal glyph_top = glyph_baseline + gtbr.top();
        qreal rect_top  = std::min(y, glyph_top) - pad_v;
        qreal rect_h    = (y + row_h) - rect_top + pad_v;

        out_layout->tempo_rect = QRectF(start_x - pad_h,
                                        rect_top,
                                        total_w + pad_h * 2.0,
                                        rect_h);

        // Sub-rects so the glyph and the BPM number can be clicked
        // independently.  Glyph hit-target hugs the glyph; BPM hit-target
        // covers the " = NNN" text (the equals sign goes with the number
        // so the clickable region looks visually balanced).
        out_layout->tempo_glyph_rect = QRectF(start_x - pad_h,
                                              rect_top,
                                              glyph_w + pad_h,
                                              rect_h);
        out_layout->tempo_bpm_rect   = QRectF(start_x + glyph_w,
                                              rect_top,
                                              text_w + pad_h,
                                              rect_h);
    }

    painter.restore();
}

// ---------------------------------------------------------------------------
// parse_key
// ---------------------------------------------------------------------------
void margin_renderer::parse_key(const std::string& key,
                               QString& letter,
                               QString& accidental,
                               QString& suffix)
{
    letter     = "";
    accidental = "";
    suffix     = "";

    if (key.empty())
    {
        letter = "?";
        return;
    }

    // First character is the note letter (rendered uppercase).
    letter = QString(QChar(key[0])).toUpper();

    // If the second character is a flat or sharp marker, substitute the
    // proper Unicode glyph; the rest of the string (if any) becomes the
    // suffix.  This preserves nice rendering of canonical short forms like
    // "Bb" → "B♭" or "F#min" → "F♯min" while still letting users write
    // anything they like ("A min", "A Minor", "C dorian", …).
    std::size_t consumed = 1;
    if (key.size() > 1)
    {
        char acc = key[1];
        if (acc == 'b' || acc == 'B')
        {
            accidental = QString(k_flat);
            consumed = 2;
        }
        else if (acc == '#')
        {
            accidental = QString(k_sharp);
            consumed = 2;
        }
    }

    if (consumed < key.size())
        suffix = QString::fromStdString(key.substr(consumed));
}

// ---------------------------------------------------------------------------
// tempo_glyph
// ---------------------------------------------------------------------------
QString margin_renderer::tempo_glyph(model::chord::time beat_unit)
{
    // SMuFL metronome marks (U+ECA0 block).  These are purpose-built for
    // "note = NNN" tempo indications: each is a complete note with the
    // correct stem and flag, drawn at a consistent optical weight.  The
    // plain Unicode music characters (U+2669 ♩, U+266A ♪) have no whole- or
    // half-note counterparts, which is why the previous mixed approach fell
    // back to bare noteheads for those two values.
    switch (beat_unit)
    {
        case model::chord::time::WHOLE:
            return "\uECA2"; // metNoteWhole (stemless by design)
        case model::chord::time::HALF:
        case model::chord::time::DOTTED_HALF:
            return "\uECA3"; // metNoteHalfUp
        case model::chord::time::QUARTER:
        case model::chord::time::DOTTED_QUARTER:
            return "\uECA5"; // metNoteQuarterUp
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:
            return "\uECA7"; // metNote8thUp
        case model::chord::time::SIXTEENTH:
            return "\uECA9"; // metNote16thUp
        default:
            return "\uECA5";
    }
}

// ---------------------------------------------------------------------------
// tempo_is_dotted
// ---------------------------------------------------------------------------
bool margin_renderer::tempo_is_dotted(model::chord::time beat_unit)
{
    return beat_unit == model::chord::time::DOTTED_EIGHTH  ||
           beat_unit == model::chord::time::DOTTED_QUARTER ||
           beat_unit == model::chord::time::DOTTED_HALF;
}

} // namespace nashville::view
