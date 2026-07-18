#include "chord_renderer.hpp"
#include <QFontMetricsF>
#include <QPainterPath>
#include <cmath>
#include <algorithm>

namespace nashville::view
{

// ---------------------------------------------------------------------------
// Unicode glyphs
// ---------------------------------------------------------------------------
static constexpr const char* kFlat     = "\u266D"; // ♭
static constexpr const char* kSharp    = "\u266F"; // ♯
static constexpr const char* kDiminish = "\u00B0"; // °

// Returns the extension string rendered with proper sharp/flat glyphs
// substituted for the ASCII 'b' and '#' the user types.  Nashville
// notation uses scale degrees (numbers) rather than note letters in
// extensions, so a literal 'b' inside extensions is unambiguously
// "flat" — there's no chance of colliding with a note name.  The
// substitution is rendering-only: the model still stores the raw
// ASCII the user typed, so to_user_input() round-trips cleanly and the
// inline editor still seeds with ASCII the user can type easily.
//
// Two passes is fine — extension strings are tiny (typically a handful
// of characters like "b9" or "7#11") so the linear walk is trivially
// cheap, and there's no order-dependence between '#' and 'b' (neither
// substitution introduces or destroys instances of the other).
static QString prettify_extensions(const std::string& ext_str)
{
    QString s = QString::fromStdString(ext_str);
    s.replace(QLatin1Char('#'), QChar(0x266F));  // ♯
    s.replace(QLatin1Char('b'), QChar(0x266D));  // ♭
    return s;
}

// ---------------------------------------------------------------------------
// Public: size_hint
// ---------------------------------------------------------------------------
QSizeF chord_renderer::size_hint(const model::chord& ch, const Fonts& fonts)
{
    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF modFm(fonts.modifier);

    // A rest occupies no chord-number zone; its width is just the rest glyph
    // (plus an augmentation dot when dotted) drawn in the rhythm row.  Height
    // stays the standard chord-slot height so the row lines up with neighbours.
    if (ch.is_rest())
    {
        const model::chord::time dur =
            ch.duration().value_or(model::chord::time::WHOLE);
        QFont rf = rest_font(fonts, k_rhythm_row_px);
        QFontMetricsF rfm(rf);
        qreal w = rfm.tightBoundingRect(rest_glyph_for(dur)).width();
        if (is_dotted(dur))
            w += k_element_spacing + 2.0 * 1.8;   // dot diameter
        w += 4.0;                                  // a little horizontal breathing room
        qreal total_height = nmFm.height() / k_number_zone_ratio;
        return QSizeF(w, total_height);
    }

    qreal row_width = 0;

    if (ch.step())
        row_width += nmFm.horizontalAdvance(kFlat) + k_element_spacing;

    row_width += nmFm.horizontalAdvance(QString::number(ch.number()));

    switch (ch.mode())
    {
        case model::chord::type::MINOR:
            row_width += modFm.horizontalAdvance("-") + k_element_spacing;
            break;
        case model::chord::type::DIMINISHED:
            row_width += modFm.horizontalAdvance(kDiminish) + k_element_spacing;
            break;
        case model::chord::type::AUGMENTED:
            row_width += modFm.horizontalAdvance("+") + k_element_spacing;
            break;
        default: break;
    }

    if (!ch.extensions().empty())
        row_width += modFm.horizontalAdvance(
                        prettify_extensions(ch.extensions())) + k_element_spacing;

    if (ch.bass_note())
    {
        // Matches paint_bass_note: slash, its accidental, and the bass digit
        // all draw at number size, so their reserved width comes from nmFm.
        row_width += nmFm.horizontalAdvance("/") + k_element_spacing;
        if (ch.bass_note_step())
            row_width += nmFm.horizontalAdvance(kFlat) + k_element_spacing;
        row_width += nmFm.horizontalAdvance(QString::number(*ch.bass_note()));
    }

    qreal number_height = nmFm.height();
    qreal total_height  = number_height / k_number_zone_ratio;

    return QSizeF(row_width, total_height);
}

// ---------------------------------------------------------------------------
// Public: paint
// ---------------------------------------------------------------------------
void chord_renderer::paint(QPainter& painter,
                          const QRectF& rect,
                          const model::chord& ch,
                          const Fonts& fonts,
                          bool is_duration_mode,
                          bool line_has_articulation)
{
    if (ch.mode() == model::chord::type::UNDEFINED)
        return;

    // A rest carries no chord symbol above the rule — it is drawn entirely in
    // the rhythm row by paint_rhythm().  Returning here keeps the REST sentinel
    // (number 0) from leaking into the chord-number row as a literal "0", and
    // suppresses articulations/diamond, which don't apply to a rest.
    if (ch.is_rest())
        return;

    painter.save();

    QFontMetricsF art_fm(fonts.articulation);
    qreal art_height = line_has_articulation
                       ? art_fm.ascent() + art_fm.descent()
                       : 0.0;
    constexpr qreal k_plain_top_pad = 4.0;
    qreal top_offset = art_height > 0.0 ? art_height : k_plain_top_pad;
    QRectF artRect(rect.left(), rect.top(), rect.width(), art_height);
    QRectF numRect(rect.left(), rect.top() + top_offset,
                   rect.width(), rect.height() - top_offset);

    // Paint number row; get tight rect around the number glyph (for
    // articulation centring) and the full symbol's ink extent (for the
    // diamond, so extensions/mode/bass sit inside it).
    qreal row_right = 0.0;
    QRectF chordExtent;
    QRectF numberGlyphRect =
        paint_number_row(painter, numRect, ch, fonts, row_right, chordExtent);

    if (line_has_articulation)
        paint_articulations(painter, artRect, ch, fonts,
                            numberGlyphRect.center().x(), row_right);

    if (ch.is_diamond())
        paint_diamond(painter, chordExtent, artRect.bottom(), numRect.bottom() - 1.0, fonts);

    painter.restore();
}

// ---------------------------------------------------------------------------
// Public: paintRhythm
// ---------------------------------------------------------------------------
chord_renderer::StemInfo chord_renderer::paint_rhythm(QPainter& painter,
                                  const QRectF& rect,
                                  const model::chord& ch,
                                  const Fonts& fonts,
                                  bool suppress_flag)
{
    // Rests render in the rhythm row regardless of whether a duration is set
    // (no duration -> whole rest), so they're handled before the duration
    // guard below.
    if (ch.is_rest())
    {
        paint_rest(painter, rect, ch, fonts);
        return StemInfo{};
    }

    if (!ch.duration())
        return StemInfo{};

    const bool dotted  = is_dotted(*ch.duration());
    const bool is_half = (*ch.duration() == model::chord::time::HALF ||
                          *ch.duration() == model::chord::time::DOTTED_HALF);
    const bool is_whole = (*ch.duration() == model::chord::time::WHOLE);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);

    QColor ink = painter.pen().color();
    constexpr qreal k_stem_gap = 3.0;

    // All glyphs drawn from Bravura. Use the notehead glyph to size the font,
    // then draw the stem and flags as primitives/separate glyphs.
    // noteheadBlack = U+E0A4, noteheadHalf = U+E0A3, noteheadWhole = U+E0A2
    QString head_glyph = is_whole ? "\uE0A2" : (is_half ? "\uE0A3" : "\uE0A4");

    QFont glyphFont = fonts.music;

    // Scale so the notehead occupies ~65% of the row height.
    {
        const qreal px_per_pt = 96.0 / 72.0;
        const qreal target_px = rect.height() * 0.65;
        qreal pt_size = target_px / (0.3 * px_per_pt);
        glyphFont.setPointSizeF(pt_size);
        QFontMetricsF fm0(glyphFont);
        qreal ascent_px = fm0.ascent();
        if (ascent_px > 0.0)
        {
            qreal head_px = ascent_px * 0.28;
            if (head_px > 0.0)
                glyphFont.setPointSizeF(pt_size * (target_px / head_px));
        }
    }

    painter.setFont(glyphFont);
    QFontMetricsF fm(glyphFont);

    qreal notehead_h = fm.ascent() * 0.28;

    // Clip k_stem_gap below rect.top() so stems stop short of the rule.
    painter.setClipRect(rect.adjusted(0, k_stem_gap, 0, 0));

    // Baseline: place so the notehead bottom is near rect.bottom().
    qreal baseline = rect.bottom() - notehead_h * 0.25;

    // Compensate for any negative left bearing in the Bravura glyph.
    QRectF head_tbr = fm.tightBoundingRect(head_glyph);
    qreal draw_x = rect.left() - std::min(0.0, head_tbr.left());

    painter.setPen(QPen(ink, 1.0));
    painter.drawText(QPointF(draw_x, baseline), head_glyph);

    qreal head_top_y    = baseline + head_tbr.top();
    qreal head_bottom_y = baseline + head_tbr.bottom();
    qreal head_right_x  = draw_x + head_tbr.right();

    // Stem: all notes except whole get a manual upward stem from the right edge.
    qreal stem_x  = head_right_x - 1.0;
    qreal stem_y1 = rect.top() + k_stem_gap;   // top of stem (near rule)
    if (!is_whole)
    {
        qreal stem_y0 = head_top_y + 1.0;
        painter.setPen(QPen(ink, 1.0));
        painter.drawLine(QPointF(stem_x, stem_y0), QPointF(stem_x, stem_y1));
    }

    // Flag glyph drawn at the top of the stem, scaled to fit within the stem length.
    // flag8thUp = U+E240, flag16thUp = U+E242. Skipped when this note is part
    // of a beam group — bar_renderer draws a shared beam across the group
    // instead, in the same visual position a flag would otherwise occupy.
    QString flag_glyph;
    if (!suppress_flag)
    {
        switch (*ch.duration())
        {
            case model::chord::time::EIGHTH:
            case model::chord::time::DOTTED_EIGHTH:  flag_glyph = "\uE240"; break;
            case model::chord::time::SIXTEENTH:      flag_glyph = "\uE242"; break;
            default: break;
        }
    }
    if (!flag_glyph.isEmpty())
    {
        // Scale the flag so its height fits the stem length.
        qreal available_h = head_top_y - stem_y1;
        QFont flagFont = glyphFont;
        {
            QFontMetricsF fm0(flagFont);
            qreal flag_h = fm0.tightBoundingRect(flag_glyph).height();
            if (flag_h > 0.0 && available_h > 0.0)
                flagFont.setPointSizeF(flagFont.pointSizeF() * (available_h / flag_h));
        }
        painter.setFont(flagFont);
        painter.setPen(QPen(ink, 1.0));
        QFontMetricsF flag_fm(flagFont);
        QRectF flag_tbr = flag_fm.tightBoundingRect(flag_glyph);
        // Draw so the top of the flag sits at stem_y1.
        painter.drawText(QPointF(stem_x, stem_y1 - flag_tbr.top()), flag_glyph);
        painter.setFont(glyphFont);  // restore for dot advance-width calc
    }

    // Augmentation dot: right of notehead, centred vertically on the notehead.
    if (dotted)
    {
        qreal dot_r = 1.8;  // fixed radius; proportional (rect.height()*0.06) was too thick
        qreal dot_x = draw_x + fm.horizontalAdvance(head_glyph) + k_element_spacing + dot_r;
        qreal dot_y = (head_top_y + head_bottom_y) / 2.0;  // centre of notehead for all types
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(dot_x, dot_y), dot_r, dot_r);
    }

    painter.restore();

    StemInfo info;
    info.has_stem   = !is_whole;
    info.stem_x     = stem_x;
    info.beam_y     = stem_y1;
    info.notehead_h = notehead_h;
    return info;
}

// ---------------------------------------------------------------------------
// Public: paint_articulations
// Shared articulation dispatcher used by both paint() (plain mode) and
// bar_renderer (duration mode, where artRect lives above the chord slot).
// ---------------------------------------------------------------------------
void chord_renderer::paint_articulations(QPainter& painter,
                                         const QRectF& artRect,
                                         const model::chord& ch,
                                         const Fonts& fonts,
                                         qreal number_center_x,
                                         qreal row_right)
{
    if (ch.is_staccato() && ch.is_pushed())
    {
        qreal mid = artRect.top() + artRect.height() / 2.0;
        QRectF staccatoRect(artRect.left(), artRect.top(), artRect.width(), artRect.height() / 2.0);
        QRectF pushedRect(artRect.left(), mid, artRect.width(), artRect.height() / 2.0);
        paint_staccato(painter, staccatoRect, number_center_x);
        paint_pushed(painter, pushedRect, fonts, number_center_x, artRect.top());
    }
    else
    {
        if (ch.is_staccato())
            paint_staccato(painter, artRect, number_center_x);
        if (ch.is_pushed())
            paint_pushed(painter, artRect, fonts, number_center_x, artRect.top());
    }

    if (ch.is_tied())
    {
        // The tie arc starts past the right edge of the full chord row
        // (number + mode + extensions + bass note).  This is correct for
        // both plain and diamond chords: row_right is always past the
        // diamond's right edge since the diamond surrounds only the
        // number glyph, which sits at the left of the row.
        paint_tied_arc(painter, artRect, /*diamond_left=*/-1.0, /*diamond_right=*/row_right);
    }
}

// ---------------------------------------------------------------------------
// Private: paint_number_row
// ---------------------------------------------------------------------------
QRectF chord_renderer::paint_number_row(QPainter& painter,
                                      const QRectF& rowRect,
                                      const model::chord& ch,
                                      const Fonts& fonts,
                                      qreal& row_right,
                                      QRectF& chord_extent)
{
    QFontMetricsF nmFm(fonts.number);
    QFontMetricsF modFm(fonts.modifier);

    // Compute total row width for centering
    qreal total_width = 0;
    if (ch.step())
        total_width += nmFm.horizontalAdvance(kFlat) + k_element_spacing;
    total_width += nmFm.horizontalAdvance(QString::number(ch.number()));
    if (ch.mode() == model::chord::type::MINOR)
        total_width += modFm.horizontalAdvance("-") + k_element_spacing;
    else if (ch.mode() == model::chord::type::DIMINISHED)
        total_width += modFm.horizontalAdvance(kDiminish) + k_element_spacing;
    else if (ch.mode() == model::chord::type::AUGMENTED)
        total_width += modFm.horizontalAdvance("+") + k_element_spacing;
    if (!ch.extensions().empty())
        total_width += modFm.horizontalAdvance(
                          prettify_extensions(ch.extensions())) + k_element_spacing;
    if (ch.bass_note())
    {
        // Slash, its accidental, and the bass digit all draw at number size
        // now (see paint_bass_note), so their width is reserved against nmFm.
        total_width += nmFm.horizontalAdvance("/") + k_element_spacing;
        if (ch.bass_note_step())
            total_width += nmFm.horizontalAdvance(kFlat) + k_element_spacing;
        total_width += nmFm.horizontalAdvance(QString::number(*ch.bass_note()));
    }

    // Compute baseline so the tight ink bounds of the number are centred in rowRect.
    QString num_str = QString::number(ch.number());
    QRectF tbr = nmFm.tightBoundingRect(num_str);
    // tbr.top() is negative (above baseline), tbr.bottom() is positive (below).
    // Centre: rowRect.center().y() == baseline + (tbr.top() + tbr.bottom()) / 2
    qreal baseline = rowRect.center().y() - (tbr.top() + tbr.bottom()) / 2.0;
    qreal x = rowRect.left();

    painter.save();

    // Accumulate the tight ink bounds of every part as we draw, so the
    // diamond can be drawn around the whole symbol (not just the number).
    // A part drawn as string s at pen position (gx, gy) with font metrics fm
    // occupies fm.tightBoundingRect(s) translated to that origin.
    chord_extent = QRectF();
    auto unite_ink = [&](const QString& s, const QFontMetricsF& fm,
                         qreal gx, qreal gy) {
        if (s.isEmpty()) return;
        const QRectF tb = fm.tightBoundingRect(s);
        const QRectF ink(gx + tb.left(), gy + tb.top(), tb.width(), tb.height());
        chord_extent = chord_extent.isNull() ? ink : chord_extent.united(ink);
    };

    // Step (♭/♯) — drawn at number size now (see paint_step), and still
    // ink-centred against the number digit's ink rather than sharing its
    // baseline outright: even at matching font size, a ♭/♯ glyph's own ink
    // doesn't sit at exactly the same vertical centre as a digit's, so the
    // centring still matters — it's just a small correction now rather than
    // the large one a size mismatch previously demanded.
    QString step_glyph;
    if (ch.step())
        step_glyph = (ch.step() == model::chord::flat_sharp::FLAT)
                     ? QString(kFlat) : QString(kSharp);
    const qreal step_gy = ch.step()
                          ? accidental_baseline(baseline, nmFm, nmFm, step_glyph)
                          : baseline;
    const qreal step_gx = x;
    x += paint_step(painter, ch, fonts, x, step_gy);
    if (ch.step())
        unite_ink(step_glyph, nmFm, step_gx, step_gy);

    // Number — dominant
    painter.setFont(fonts.number);
    qreal num_left  = x;
    painter.drawText(QPointF(x, baseline), num_str);

    // Tight ink rect for diamond sizing and centring.
    QRectF numberGlyphRect(num_left + tbr.left(),
                           baseline + tbr.top(),
                           tbr.width(),
                           tbr.height());
    chord_extent = chord_extent.isNull()
                       ? numberGlyphRect
                       : chord_extent.united(numberGlyphRect);
    x += nmFm.horizontalAdvance(num_str);

    // Mode suffix (raised to the top of the number — mirror paint_mode).
    const qreal mode_gx = x;
    const qreal mode_gy = baseline - nmFm.ascent() + modFm.ascent();
    x += paint_mode(painter, ch, fonts, x, baseline);
    {
        QString mode_s;
        switch (ch.mode())
        {
            case model::chord::type::MINOR:      mode_s = "-";       break;
            case model::chord::type::DIMINISHED: mode_s = kDiminish; break;
            case model::chord::type::AUGMENTED:  mode_s = "+";       break;
            default: break;
        }
        unite_ink(mode_s, modFm, mode_gx, mode_gy);
    }

    // Extensions — superscripted
    const qreal ext_gx = x;
    const qreal ext_gy = baseline - nmFm.ascent() * 0.3;
    x += paint_extensions(painter, ch, fonts, x, ext_gy);
    if (!ch.extensions().empty())
        unite_ink(prettify_extensions(ch.extensions()), modFm, ext_gx, ext_gy);

    // Bass note (drawn on the number baseline, so it shares the number's
    // vertical band; extend the extent's right edge to cover it).
    x += paint_bass_note(painter, ch, fonts, x, baseline);

    painter.restore();

    row_right = x;
    if (ch.bass_note())
        chord_extent.setRight(std::max(chord_extent.right(), row_right));
    return numberGlyphRect;
}

// ---------------------------------------------------------------------------
// Private: paint_step
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// accidental_baseline — ink-centre a ♭/♯ glyph against a reference digit
// ---------------------------------------------------------------------------
qreal chord_renderer::accidental_baseline(qreal digit_baseline,
                                          const QFontMetricsF& ref_fm,
                                          const QFontMetricsF& glyph_fm,
                                          const QString& glyph)
{
    // "0" stands in for the digit itself — any digit has near-identical
    // vertical ink metrics in a given font, and this needs to work with a
    // font/size pair (a large number digit, a small accidental) rather than
    // one specific character.
    const QRectF ref_tbr   = ref_fm.tightBoundingRect(QStringLiteral("0"));
    const QRectF glyph_tbr = glyph_fm.tightBoundingRect(glyph);
    const qreal ref_centre   = (ref_tbr.top()   + ref_tbr.bottom())   / 2.0;
    const qreal glyph_centre = (glyph_tbr.top() + glyph_tbr.bottom()) / 2.0;
    return digit_baseline + ref_centre - glyph_centre;
}

qreal chord_renderer::paint_step(QPainter& painter,
                                const model::chord& ch,
                                const Fonts& fonts,
                                qreal x, qreal baseline)
{
    if (!ch.step())
        return 0.0;

    // Drawn at number size, not modifier size.  It sits directly beside the
    // chord number as part of the same symbol (flat-seven, sharp-four), not
    // as a smaller annotation of it the way an extension or a mode suffix
    // is — so it needs to read as a peer of the number, not a modifier on it.
    painter.setFont(fonts.number);
    QFontMetricsF fm(fonts.number);
    QString glyph = (ch.step() == model::chord::flat_sharp::FLAT)
                    ? QString(kFlat) : QString(kSharp);
    painter.drawText(QPointF(x, baseline), glyph);
    return fm.horizontalAdvance(glyph) + k_element_spacing;
}

// ---------------------------------------------------------------------------
// Private: paint_mode
// ---------------------------------------------------------------------------
qreal chord_renderer::paint_mode(QPainter& painter,
                                const model::chord& ch,
                                const Fonts& fonts,
                                qreal x, qreal baseline)
{
    painter.setFont(fonts.modifier);
    QFontMetricsF fm(fonts.modifier);
    QString suffix;

    switch (ch.mode())
    {
        case model::chord::type::MINOR:      suffix = "-";       break;
        case model::chord::type::DIMINISHED: suffix = kDiminish; break;
        case model::chord::type::AUGMENTED:  suffix = "+";       break;
        default: return 0.0;
    }

    // Align suffix to top of the number glyph rather than its baseline,
    // so -, °, + sit beside the upper portion of the chord number.
    // Caller passes the number baseline; compute number ascent from fonts.number.
    QFontMetricsF nmFm(fonts.number);
    qreal raised_baseline = baseline - nmFm.ascent() + fm.ascent();
    painter.drawText(QPointF(x, raised_baseline), suffix);
    return fm.horizontalAdvance(suffix) + k_element_spacing;
}

// ---------------------------------------------------------------------------
// Private: paint_extensions
// ---------------------------------------------------------------------------
qreal chord_renderer::paint_extensions(QPainter& painter,
                                      const model::chord& ch,
                                      const Fonts& fonts,
                                      qreal x, qreal baseline)
{
    if (ch.extensions().empty())
        return 0.0;

    painter.setFont(fonts.modifier);
    QFontMetricsF fm(fonts.modifier);
    QString ext = prettify_extensions(ch.extensions());

    // Fast path: no accidentals to nudge — draw the whole string in one
    // shot.  Avoids any chance of per-glyph drift from kerning/shaping
    // disagreeing with horizontalAdvance().
    const QChar kFlatCh(0x266D);
    const QChar kSharpCh(0x266F);
    if (!ext.contains(kFlatCh) && !ext.contains(kSharpCh))
    {
        painter.drawText(QPointF(x, baseline), ext);
        return fm.horizontalAdvance(ext) + k_element_spacing;
    }

    // Common text fonts draw ♯ / ♭ with their visual ink centre near
    // the x-height — well below the cap-height centre of a digit — so
    // a naïve same-baseline draw makes "7♯9" look like "7" and "9"
    // sitting astride a dropped "♯".  Ink-centre each accidental against a
    // reference digit in this same font (see accidental_baseline) — the
    // same routine every step glyph in the row goes through, so the leading
    // step, an extension accidental, and a bass-note accidental all read the
    // same way.
    const qreal flat_baseline  = accidental_baseline(baseline, fm, fm, QString(kFlatCh));
    const qreal sharp_baseline = accidental_baseline(baseline, fm, fm, QString(kSharpCh));


    // Walk character by character so each glyph gets its own y.  This
    // matters because painter.drawText takes a single baseline per call.
    // Advances come from horizontalAdvance() per glyph, which is what
    // the layout passes also use (size_hint / paint_number_row both
    // call horizontalAdvance on the whole prettified string), so the
    // running x stays consistent with the reserved bar width.
    qreal cur_x = x;
    for (int i = 0; i < ext.size(); ++i)
    {
        QChar c = ext.at(i);
        qreal y;
        if (c == kSharpCh)
            y = sharp_baseline;
        else if (c == kFlatCh)
            y = flat_baseline;
        else
            y = baseline;

        QString one(c);
        painter.drawText(QPointF(cur_x, y), one);
        cur_x += fm.horizontalAdvance(one);
    }

    // Total width consumed matches what the layout passes computed for
    // the whole string — fonts can have subtle differences between
    // "advance of full string" and "sum of per-char advances" due to
    // shaping, but for the BMP digits/letters/accidentals we deal with
    // here those differences are sub-pixel.  Use the whole-string
    // advance for the return so the running x in paint_number_row
    // exactly matches what size_hint reserved.
    return fm.horizontalAdvance(ext) + k_element_spacing;
}

// ---------------------------------------------------------------------------
// Private: paint_bass_note
// ---------------------------------------------------------------------------
qreal chord_renderer::paint_bass_note(QPainter& painter,
                                    const model::chord& ch,
                                    const Fonts& fonts,
                                    qreal x, qreal baseline)
{
    if (!ch.bass_note())
        return 0.0;

    // Slash, accidental, and bass digit all draw at number size — the same
    // size as the chord number itself, not the smaller modifier size used
    // for extensions and the mode suffix.  A bass note is a full alternate
    // root (chord-over-bass), not an annotation of the main number, so it
    // reads as one when it's the same size as the number rather than
    // shrunk beneath it.
    painter.setFont(fonts.number);
    QFontMetricsF fm(fonts.number);
    qreal consumed = 0;

    painter.drawText(QPointF(x, baseline), "/");
    consumed += fm.horizontalAdvance("/") + k_element_spacing;
    x += consumed;

    if (ch.bass_note_step())
    {
        QString g = (ch.bass_note_step() == model::chord::flat_sharp::FLAT)
                    ? QString(kFlat) : QString(kSharp);
        // Same-font accidental-before-digit as the leading step, so the same
        // small ink-centring correction applies (see accidental_baseline).
        const qreal g_y = accidental_baseline(baseline, fm, fm, g);
        painter.drawText(QPointF(x, g_y), g);
        qreal w = fm.horizontalAdvance(g) + k_element_spacing;
        consumed += w;
        x += w;
    }

    QString bn = QString::number(*ch.bass_note());
    painter.drawText(QPointF(x, baseline), bn);
    consumed += fm.horizontalAdvance(bn);

    return consumed;
}

// ---------------------------------------------------------------------------
// Private: paint_staccato — filled dot above everything (larger, more visible)
// ---------------------------------------------------------------------------
void chord_renderer::paint_staccato(QPainter& painter, const QRectF& artRect,
                                    qreal number_center_x)
{
    painter.save();
    constexpr qreal dot_r = k_staccato_dot_r;
    qreal cx    = number_center_x;
    qreal cy    = artRect.bottom() - dot_r - k_staccato_floor_gap;
    QPolygonF diamond;
    diamond << QPointF(cx,          cy - dot_r)
            << QPointF(cx + dot_r,  cy)
            << QPointF(cx,          cy + dot_r)
            << QPointF(cx - dot_r,  cy);
    painter.setBrush(painter.pen().color());
    painter.setPen(Qt::NoPen);
    painter.drawPolygon(diamond);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_pushed — '>' below staccato dot
// ---------------------------------------------------------------------------
void chord_renderer::paint_pushed(QPainter& painter,
                                 const QRectF& artRect,
                                 const Fonts& fonts,
                                 qreal number_center_x,
                                 qreal /*number_top_y*/)
{
    painter.save();
    painter.setFont(fonts.articulation);
    QFontMetricsF fm(fonts.articulation);
    // Centre the > vertically within the articulation zone.
    qreal baseline = artRect.bottom();
    qreal x = number_center_x - fm.horizontalAdvance(">") / 2.0;
    painter.drawText(QPointF(x, baseline), ">");
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: paint_tied_arc — smooth cubic-bezier arc, belly curves upward
// Endpoints sit at the bottom of the tie zone; arc bulges upward toward arc_top.
// ---------------------------------------------------------------------------
void chord_renderer::paint_tied_arc(QPainter& painter, const QRectF& artRect,
                                    qreal diamond_left, qreal diamond_right)
{
    painter.save();

    // arc_top is the peak of the upward bulge; base_y is the endpoint height.
    qreal arc_top  = artRect.top() + artRect.height() * k_tied_top_ratio;
    qreal arc_span = artRect.height() * k_tied_span_ratio;   // deeper arc
    qreal margin   = artRect.width() * 0.08;

    qreal base_y   = arc_top + arc_span;

    // When a diamond is present the tie arc sits entirely to its right.
    // Otherwise span the full slot width with a small margin.
    // The tie extends past the right edge of the slot with a fixed-pixel
    // overhang so it reliably reaches into the inter-bar/inter-chord gap and
    // visually connects to the next chord — even when the slot is narrow (e.g.
    // the last chord in a multi-chord bar) or the line ends with a
    // continuation dot.  A percentage-of-slot-width overhang shrank to almost
    // nothing on narrow slots and fell short of the continuation dot.
    qreal start_x = diamond_right + margin;
    qreal end_x   = artRect.right() + 10.0;  // fixed overhang past slot edge

    // Symmetric cubic bezier: endpoints at base_y, control points pushed
    // above arc_top so the actual curve peak reaches close to arc_top.
    qreal cp_y = arc_top - arc_span * k_tied_overshoot;   // slightly above arc_top
    QPainterPath path;
    path.moveTo(start_x, base_y);
    path.cubicTo(start_x + (end_x - start_x) * 0.25, cp_y,
                 start_x + (end_x - start_x) * 0.75, cp_y,
                 end_x, base_y);

    painter.setPen(QPen(painter.pen().color(), 1.0));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

// ---------------------------------------------------------------------------
// articulation_headroom — clear space at the top of the articulation zone
// ---------------------------------------------------------------------------
qreal chord_renderer::articulation_headroom(const Fonts& fonts, qreal art_h,
                                            bool has_staccato, bool has_pushed,
                                            bool has_tied,
                                            bool has_both_on_one_chord)
{
    if (art_h <= 0.0)
        return 0.0;

    // With nothing drawn the whole zone is clear.  Each articulation present
    // pulls this down to wherever its own ink starts; the lowest value wins.
    qreal head = art_h;

    if (has_staccato)
    {
        // paint_staccato centres the diamond k_staccato_dot_r +
        // k_staccato_floor_gap above the floor of the rect it is given.  For a
        // chord that is both staccato and pushed, paint_articulations hands it
        // only the top half of the zone, so its floor is at art_h / 2.
        const qreal floor_y = has_both_on_one_chord ? art_h * 0.5 : art_h;
        head = std::min(head, floor_y - (2.0 * k_staccato_dot_r
                                         + k_staccato_floor_gap));
    }

    if (has_pushed)
    {
        // paint_pushed draws '>' with the zone's floor as its text baseline,
        // so its ink rises by the glyph's own ink ascent — not the font's,
        // which would over-reserve by the ascender height no '>' uses.
        QFontMetricsF fm(fonts.articulation);
        const qreal ink_ascent = -fm.tightBoundingRect(">").top();
        head = std::min(head, art_h - ink_ascent);
    }

    if (has_tied)
    {
        // The drawn curve peaks between cp_y and arc_top; cp_y is the higher
        // of the two, so measuring to it is the safe choice.
        const qreal arc_top  = art_h * k_tied_top_ratio;
        const qreal arc_span = art_h * k_tied_span_ratio;
        head = std::min(head, arc_top - arc_span * k_tied_overshoot);
    }

    return std::max(0.0, head);
}

// ---------------------------------------------------------------------------
// diamond_bounds — bounding rect of the diamond enclosing `content`
// ---------------------------------------------------------------------------
QRectF chord_renderer::diamond_bounds(const QRectF& content, const Fonts& fonts)
{
    const qreal pad_h = diamond_padding_h(fonts);
    const qreal pad_v = diamond_padding_v(fonts);

    // Start from the padded rect, as before.
    qreal a = content.width()  * 0.5 + pad_h;   // horizontal half-axis
    qreal b = content.height() * 0.5 + pad_v;   // vertical half-axis
    if (a <= 0.0 || b <= 0.0)
        return content;

    // How much of the diamond the content's own corner uses up.  Both terms
    // compete for the same budget of 1.0, so a symbol that is wide relative to
    // its padding leaves almost nothing for the vertical term and its top
    // corners spill out through the sloping edges.  That is why the fault
    // showed up as "not tall enough" on 1maj7 and not at all on 6-: the
    // extensions make the symbol much wider without making it much taller.
    const qreal fill = (content.width() * 0.5) / a + (content.height() * 0.5) / b;

    if (fill > k_diamond_corner_fill)
    {
        // Grow both axes by the same factor, so the diamond keeps the
        // proportions the fixed paddings were tuned for instead of turning
        // into a tall spike.  Capped so a very wide symbol can't produce a
        // diamond that overruns the line spacing.
        qreal s = fill / k_diamond_corner_fill;
        if (s > k_diamond_max_growth)
            s = k_diamond_max_growth;
        a *= s;
        b *= s;
    }

    const QPointF c = content.center();
    return QRectF(c.x() - a, c.y() - b, a * 2.0, b * 2.0);
}

// ---------------------------------------------------------------------------
// Private: paint_diamond — drawn around the full chord symbol's ink
// ---------------------------------------------------------------------------
void chord_renderer::paint_diamond(QPainter& painter, const QRectF& contentRect,
                                   qreal /*art_bottom*/, qreal /*max_bottom*/,
                                   const Fonts& fonts)
{
    painter.save();
    const QRectF r = diamond_bounds(contentRect, fonts);
    const QPointF center = r.center();
    QPolygonF diamond;
    diamond << QPointF(center.x(), r.top())
            << QPointF(r.right(),  center.y())
            << QPointF(center.x(), r.bottom())
            << QPointF(r.left(),   center.y());

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(painter.pen().color(), 1.0));
    painter.drawPolygon(diamond);
    painter.restore();
}

// ---------------------------------------------------------------------------
// Private: rhythm helpers
// ---------------------------------------------------------------------------
bool chord_renderer::is_dotted(model::chord::time duration)
{
    return duration == model::chord::time::DOTTED_EIGHTH  ||
           duration == model::chord::time::DOTTED_QUARTER ||
           duration == model::chord::time::DOTTED_HALF;
}

// ---------------------------------------------------------------------------
// Private: rest_glyph_for — SMuFL codepoint per duration
// ---------------------------------------------------------------------------
// Whole and half use the leger-line variants so a short horizontal line
// disambiguates them with no staff: the whole rest's block hangs *below* its
// line, the half rest's block sits *above* its line.  Dotted durations reuse
// the same base glyph (the dot is drawn separately by paint_rest).
QString chord_renderer::rest_glyph_for(model::chord::time duration)
{
    switch (duration)
    {
        case model::chord::time::WHOLE:          return QString(QChar(0xE4F4)); // restWholeLegerLine (line above)
        case model::chord::time::HALF:
        case model::chord::time::DOTTED_HALF:    return QString(QChar(0xE4F5)); // restHalfLegerLine (line below)
        case model::chord::time::QUARTER:
        case model::chord::time::DOTTED_QUARTER: return QString(QChar(0xE4E5)); // restQuarter
        case model::chord::time::EIGHTH:
        case model::chord::time::DOTTED_EIGHTH:  return QString(QChar(0xE4E6)); // rest8th
        case model::chord::time::SIXTEENTH:      return QString(QChar(0xE4E7)); // rest16th
    }
    return QString(QChar(0xE4F4));   // unreachable; default to whole rest
}

// ---------------------------------------------------------------------------
// Private: rest_font — scale Bravura so rest sizes are consistent
// ---------------------------------------------------------------------------
// SMuFL rests have intrinsic relative sizes (a quarter rest is tall, a whole
// rest is a small block).  We pick a single font size from the *quarter* rest
// — the tallest of the set — so it fills k_rest_target_ratio of the row, then
// draw every rest at that size.  Measuring rather than assuming pt==px keeps
// the result correct regardless of the device DPI.
QFont chord_renderer::rest_font(const Fonts& fonts, qreal row_h_px)
{
    QFont f = fonts.music;
    if (row_h_px <= 0.0)
        return f;
    f.setPointSizeF(row_h_px);   // initial guess; rescaled below
    QFontMetricsF fm0(f);
    const qreal ref_h = fm0.tightBoundingRect(QString(QChar(0xE4E5))).height(); // quarter rest
    if (ref_h > 0.0)
        f.setPointSizeF(f.pointSizeF() * (row_h_px * k_rest_target_ratio / ref_h));
    return f;
}

// ---------------------------------------------------------------------------
// Private: paint_rest — draw a rest glyph centred in the rhythm row
// ---------------------------------------------------------------------------
void chord_renderer::paint_rest(QPainter& painter,
                                const QRectF& rect,
                                const model::chord& ch,
                                const Fonts& fonts)
{
    const model::chord::time dur =
        ch.duration().value_or(model::chord::time::WHOLE);
    const bool dotted = is_dotted(dur);
    const QString glyph = rest_glyph_for(dur);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const QColor ink = painter.pen().color();

    QFont mf = rest_font(fonts, rect.height());
    painter.setFont(mf);
    QFontMetricsF fm(mf);

    const QRectF tbr = fm.tightBoundingRect(glyph);

    // Centre the glyph (plus the dot, when present) within the slot, both
    // horizontally and vertically, so it reads as belonging to this column.
    const qreal dot_r     = 1.8;
    const qreal dot_space = dotted ? (k_element_spacing + 2.0 * dot_r) : 0.0;
    const qreal total_w   = tbr.width() + dot_space;
    const qreal ink_left  = rect.center().x() - total_w / 2.0;
    const qreal draw_x    = ink_left - tbr.left();
    const qreal draw_y    = rect.center().y() - (tbr.top() + tbr.height() / 2.0);

    painter.setPen(QPen(ink, 1.0));
    painter.drawText(QPointF(draw_x, draw_y), glyph);

    if (dotted)
    {
        const qreal dot_x = draw_x + tbr.right() + k_element_spacing + dot_r;
        const qreal dot_y = rect.center().y();
        painter.setPen(Qt::NoPen);
        painter.setBrush(ink);
        painter.drawEllipse(QPointF(dot_x, dot_y), dot_r, dot_r);
    }

    painter.restore();
}

} // namespace nashville::view
