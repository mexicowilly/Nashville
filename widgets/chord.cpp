#include "chord.hpp"
#include "../settings.hpp"
#include <QLabel>
#include <QTextStream>
#include <QVBoxLayout>
#include <QMouseEvent>

namespace
{

const QString FLAT_SIGN(u'\u266d');
const QString SHARP_SIGN(u'\u266f');
const QString DIMINISHED_SYMBOL(u'\u26ac');
const QString MAJOR_SEVENTH_SYMBOL(u'\u0394');

}

namespace nashville::widgets
{

chord::chord(QWidget* parent)
    : chord(parent, model::chord())
{
}

chord::chord(QWidget* parent, const model::chord& mdl)
    : QWidget(parent),
      model_(mdl)
{
    setLayout(new QVBoxLayout);
    chord_text_ = new QLabel();
    layout()->addWidget(chord_text_);
    underbar_ = new QWidget();
    QSettings settings;
    auto thickness = settings.value(settings::BAR_UNDERLINE_THICKNESS,
                                    settings::defaults::BAR_UNDERLINE_THICKNESS).toInt();
    underbar_->setFixedHeight(thickness);
    underbar_->setAutoFillBackground(true);
    layout()->addWidget(underbar_);
    underlined(false);
    setAutoFillBackground(true);
    set_chord_label_text();
}

chord& chord::selected(bool state)
{
    if (state != selected_)
    {
        selected_ = state;
        QPalette pal = palette();
        pal.setColor(backgroundRole(), (selected_ ? Qt::lightGray : Qt::white));
        setPalette(pal);   
    }
    return *this;
}

void chord::set_chord_label_text()
{
    if (model_.mode() == model::chord::type::UNDEFINED)
    {
        chord_text_->setText("");
    }
    else
    {
        QSettings settings;
        QTextStream out;
        if (model_.step())
            out << (*model_.step() == model::chord::flat_sharp::FLAT ? FLAT_SIGN : SHARP_SIGN);
        out << model_.number();
        auto minor_style = settings.value(settings::MINOR_CHORD_STYLE,
                                          settings::defaults::MINOR_CHORD_STYLE).toInt();
        auto use_symbols = settings.value(settings::SHOW_CHORD_SYMBOLS,
                                          settings::defaults::SHOW_CHORD_SYMBOLS).toBool();
        switch (model_.mode())
        {
        case model::chord::type::MINOR:
            switch (minor_style)
            {
            case settings::MINOR_DASH:
                out << '-';
                break;
            case settings::MINOR_LETTER_M:
                out << 'm';
                break;
            case settings::MINOR_ABBREV_MIN:
                out << "min";
            }
            break;
        case model::chord::type::DIMINISHED:
            out << (use_symbols ? DIMINISHED_SYMBOL : "dim");
            break;
        case model::chord::type::AUGMENTED:
            out << '+';
            break;
        default:;
        }
        if (!model_.extensions().empty())
        {
            out << "<sup>";
            QString ext(QString::fromStdString(model_.extensions()));
            if (use_symbols)
            {
                ext.replace("maj7", MAJOR_SEVENTH_SYMBOL, Qt::CaseInsensitive);
                ext.replace("maj", MAJOR_SEVENTH_SYMBOL, Qt::CaseInsensitive);
            }
            ext.replace("b", FLAT_SIGN);
            ext.replace("#", SHARP_SIGN);
            out << ext;
            out << "</sup>";
        }
        if (model_.bass_note())
        {
            out << "<b>/</b>";
            if (model_.bass_note_step())
                out << (*model_.bass_note_step() == model::chord::flat_sharp::FLAT ? FLAT_SIGN : SHARP_SIGN);
            out << *model_.bass_note();
        }
        std::unique_ptr<QString> txt(out.string());
        chord_text_->setText(*txt);
    }
}

chord& chord::underline_thickness(unsigned th)
{
    underbar_->setFixedHeight(th);
    return *this;
}

chord& chord::underlined(bool state)
{
    QPalette pal = underbar_->palette();
    pal.setColor(backgroundRole(), (state ? Qt::black : Qt::white));
    underbar_->setPalette(pal);   
    return *this;
}

}
