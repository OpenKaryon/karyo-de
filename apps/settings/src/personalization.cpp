// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization (screen 1h): theme, accent, background, taskbar, Start
// layout, input language, transparency and fonts.
#include "config.h"
#include "pages.h"
#include "widgets.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QColorDialog>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

// Strings of this page and its sub-pages, in the Personalization context.
class Text
{
    Q_DECLARE_TR_FUNCTIONS(Personalization)
};

// Mini desktop of the hero card: the wallpaper as chosen, a window and the
// taskbar's dots, in the current theme.
class DesktopPreview : public QWidget
{
public:
    explicit DesktopPreview(QWidget *parent)
        : QWidget(parent)
    {
        setFixedSize(300, 186);
        const QSettings desktop = Config::desktop();
        m_mode = desktop.value(QStringLiteral("Background/mode"), QStringLiteral("organic")).toString();
        m_color = QColor(desktop.value(QStringLiteral("Background/color")).toString());
        if (m_mode == QLatin1String("picture")) {
            QImageReader reader(desktop.value(QStringLiteral("Background/picture")).toString());
            reader.setAutoTransform(true);
            if (reader.size().isValid())
                reader.setScaledSize(reader.size().scaled(size(), Qt::KeepAspectRatioByExpanding));
            m_picture = reader.read();
        }
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(rect()), 20, 20);
        p.setClipPath(clip);
        if (m_mode == QLatin1String("solid") && m_color.isValid()) {
            p.fillRect(rect(), m_color);
        } else if (m_mode == QLatin1String("picture") && !m_picture.isNull()) {
            p.drawImage(QPoint((width() - m_picture.width()) / 2, (height() - m_picture.height()) / 2), m_picture);
        } else {
            p.fillRect(rect(), t.desk);
            const qreal s = height() / 1080.0;
            p.setPen(Qt::NoPen);
            p.setBrush(t.blobA);
            p.drawEllipse(QRectF(-300 * s, height() - (-460 + 1180) * s, 1180 * s, 1180 * s));
            p.setBrush(t.blobB);
            p.drawEllipse(QRectF(250 * s, height() - (40 + 420) * s, 420 * s, 420 * s));
            p.setBrush(t.blobD);
            p.drawEllipse(QRectF(width() - (-180 + 700) * s, -220 * s, 700 * s, 700 * s));
        }
        // A window with an accent title bar line.
        const QRectF window(width() * 0.28, height() * 0.16, width() * 0.5, height() * 0.5);
        Ui::fillRounded(p, window, 10, t.win, t.edge);
        Ui::fillPill(p, QRectF(window.x() + 12, window.y() + 16, window.width() * 0.6, 7), t.accent);
        Ui::fillPill(p, QRectF(window.x() + 12, window.y() + 32, window.width() * 0.78, 4), t.edge);
        // Taskbar.
        p.fillRect(QRectF(0, height() - 22, width(), 22), t.glass);
        const QColor dots[4] = {t.accent, Ui::accent(300), Ui::sage(300), Ui::neutral(800)};
        for (int i = 0; i < 4; ++i)
            Ui::fillPill(p, QRectF(width() / 2.0 - 26 + i * 14, height() - 15, 8, 8), dots[i]);
        p.setClipping(false);
        p.setPen(QPen(t.edge, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
    }

private:
    QString m_mode;
    QColor m_color;
    QImage m_picture;
};

// Light / Dark / Follow sunset: a two-color sample over a radio and the name.
class ThemeCard : public QAbstractButton
{
public:
    ThemeCard(const QString &name, QColor left, QColor right, QWidget *parent)
        : QAbstractButton(parent)
        , m_left(left)
        , m_right(right)
    {
        setText(name);
        setCheckable(true);
        setAutoExclusive(true);
        setAttribute(Qt::WA_Hover);
        setCursor(Qt::PointingHandCursor);
        setMinimumWidth(150);
        setFixedHeight(124);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF card = QRectF(rect()).adjusted(2, 2, -2, -2);
        Ui::fillRounded(p, card, 8, underMouse() ? Ui::mix(t.text, t.win, 4) : t.win);
        p.setPen(isChecked() ? QPen(t.accent, 2) : QPen(t.edge, 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(card, 8, 8);
        // Sample: two halves in a rounded rect.
        const QRectF sample = card.adjusted(10, 10, -10, -46);
        QPainterPath clip;
        clip.addRoundedRect(sample, 12, 12);
        p.save();
        p.setClipPath(clip);
        p.fillRect(QRectF(sample.left(), sample.top(), sample.width() / 2, sample.height()), m_left);
        p.fillRect(QRectF(sample.center().x(), sample.top(), sample.width() / 2, sample.height()), m_right);
        p.restore();
        p.setPen(QPen(t.edge, 1));
        p.drawRoundedRect(sample, 12, 12);
        // Radio and name.
        const QRectF radio(card.left() + 14, card.bottom() - 30, 16, 16);
        p.setPen(QPen(isChecked() ? t.accent : t.muted, 1.5));
        p.drawEllipse(radio);
        if (isChecked())
            Ui::fillPill(p, radio.adjusted(4, 4, -4, -4), t.accent);
        p.setFont(Ui::bodyFont(13.5, QFont::DemiBold));
        p.setPen(t.text);
        p.drawText(QRectF(radio.right() + 10, radio.top() - 4, card.width() - 50, 24), Qt::AlignVCenter, text());
    }

private:
    QColor m_left;
    QColor m_right;
};

// A 34px color circle; the chosen one gets a double ring.
class Swatch : public QAbstractButton
{
public:
    Swatch(const QColor &color, QWidget *parent)
        : QAbstractButton(parent)
        , m_color(color)
    {
        setCheckable(true);
        setAutoExclusive(true);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(44, 44);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF dot(5, 5, 34, 34);
        if (isChecked()) {
            p.setPen(QPen(m_color, 2));
            p.setBrush(Qt::NoBrush);
            p.drawEllipse(dot.adjusted(-4, -4, 4, 4));
        }
        Ui::fillPill(p, dot, m_color);
        if (underMouse() && !isChecked()) {
            p.setPen(QPen(t.edge, 1));
            p.drawEllipse(dot);
        }
    }

private:
    QColor m_color;
};

class HeroCard : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        Ui::fillRounded(p, rect(), 8, Ui::Theme::current().card);
    }
};

QLabel *label(const QString &text, QWidget *parent)
{
    auto *l = new QLabel(text, parent);
    l->setFont(Ui::bodyFont(14, QFont::DemiBold));
    return l;
}

SettingsPage *backgroundPage(QWidget *parent)
{
    auto *page = new SettingsPage(Text::tr("Background"), parent);
    QSettings desktop = Config::desktop();
    const QString mode = desktop.value(QStringLiteral("Background/mode"), QStringLiteral("organic")).toString();
    const QStringList modes = {QStringLiteral("organic"), QStringLiteral("solid"), QStringLiteral("picture")};

    auto *preview = new DesktopPreview(page);
    page->column()->addWidget(preview);

    auto *modeRow = new SettingRow(QStringLiteral("image"), Text::tr("Personalize your background"),
                                   Text::tr("Organic shapes follow the theme and accent"), page);
    auto *modeSeg = new Segmented({Text::tr("Organic shapes"),
                                   Text::tr("Solid color"),
                                   Text::tr("Picture")});
    modeSeg->setCurrentIndex(qMax(0, int(modes.indexOf(mode))));
    modeRow->setControl(modeSeg);
    page->column()->addWidget(modeRow);

    // Solid colors: the desk tones and a custom one.
    auto *colorRow = new SettingRow(QStringLiteral("palette"), Text::tr("Color"), QString(), page);
    auto *colors = new QWidget;
    auto *colorsLayout = new QHBoxLayout(colors);
    colorsLayout->setContentsMargins(0, 0, 0, 0);
    colorsLayout->setSpacing(2);
    const QString current = desktop.value(QStringLiteral("Background/color")).toString();
    for (const QColor &c : {QColor(0xf5, 0xea, 0xd8), Ui::sage(200), Ui::accent(200), Ui::neutral(300),
                            Ui::neutral(800), QColor(0x2a, 0x33, 0x2b)}) {
        auto *swatch = new Swatch(c, colors);
        swatch->setChecked(c.name() == current);
        QObject::connect(swatch, &QAbstractButton::clicked, page, [c] {
            QSettings d = Config::desktop();
            d.setValue(QStringLiteral("Background/mode"), QStringLiteral("solid"));
            d.setValue(QStringLiteral("Background/color"), c.name());
        });
        colorsLayout->addWidget(swatch);
    }
    auto *custom = new QPushButton(Text::tr("Custom…"), colors);
    QObject::connect(custom, &QPushButton::clicked, page, [page, current] {
        const QColor c = QColorDialog::getColor(QColor(current), page);
        if (!c.isValid())
            return;
        QSettings d = Config::desktop();
        d.setValue(QStringLiteral("Background/mode"), QStringLiteral("solid"));
        d.setValue(QStringLiteral("Background/color"), c.name());
    });
    colorsLayout->addWidget(custom);
    colorRow->setControl(colors);
    colorRow->setVisible(mode == QLatin1String("solid"));
    page->column()->addWidget(colorRow);

    auto *pictureRow = new SettingRow(QStringLiteral("image"), Text::tr("Picture"),
                                      desktop.value(QStringLiteral("Background/picture")).toString(), page);
    auto *browse = new QPushButton(Text::tr("Browse photos"));
    QObject::connect(browse, &QPushButton::clicked, page, [page] {
        const QString start = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
        const QString file = QFileDialog::getOpenFileName(
            page, Text::tr("Choose a picture"), start,
            Text::tr("Images (*.png *.jpg *.jpeg *.webp *.svg)"));
        if (file.isEmpty())
            return;
        QSettings d = Config::desktop();
        d.setValue(QStringLiteral("Background/mode"), QStringLiteral("picture"));
        d.setValue(QStringLiteral("Background/picture"), file);
    });
    pictureRow->setControl(browse);
    pictureRow->setVisible(mode == QLatin1String("picture"));
    page->column()->addWidget(pictureRow);

    auto *fillRow = new SettingRow(QStringLiteral("layout-grid"), Text::tr("Choose a fit"), QString(), page);
    const QStringList fills = {QStringLiteral("fill"), QStringLiteral("fit"), QStringLiteral("center")};
    auto *fillSeg = new Segmented({Text::tr("Fill"),
                                   Text::tr("Fit"),
                                   Text::tr("Center")});
    fillSeg->setCurrentIndex(qMax(0, int(fills.indexOf(desktop.value(QStringLiteral("Background/fill")).toString()))));
    QObject::connect(fillSeg, &Segmented::currentChanged, page,
                     [fills](int i) { Config::desktop().setValue(QStringLiteral("Background/fill"), fills.at(i)); });
    fillRow->setControl(fillSeg);
    fillRow->setVisible(mode == QLatin1String("picture"));
    page->column()->addWidget(fillRow);

    QObject::connect(modeSeg, &Segmented::currentChanged, page, [=](int i) {
        colorRow->setVisible(i == 1);
        pictureRow->setVisible(i == 2);
        fillRow->setVisible(i == 2);
        QSettings d = Config::desktop();
        // A picture needs a file first; Browse sets the mode once one is chosen.
        if (i == 2 && d.value(QStringLiteral("Background/picture")).toString().isEmpty())
            return;
        if (i == 1 && !QColor(d.value(QStringLiteral("Background/color")).toString()).isValid())
            d.setValue(QStringLiteral("Background/color"), QColor(0xf5, 0xea, 0xd8).name());
        d.setValue(QStringLiteral("Background/mode"), modes.at(i));
    });
    return page;
}

SettingsPage *taskbarPage(QWidget *parent)
{
    auto *page = new SettingsPage(Text::tr("Taskbar"), parent);
    QSettings shell = Config::shell();
    auto *align = new SettingRow(QStringLiteral("panel-bottom"), Text::tr("Taskbar alignment"),
                                 Text::tr("Where Start and the apps sit"), page);
    auto *seg = new Segmented({Text::tr("Center"),
                               Text::tr("Left")});
    seg->setCurrentIndex(shell.value(QStringLiteral("Taskbar/alignment")).toString() == QLatin1String("left") ? 1 : 0);
    QObject::connect(seg, &Segmented::currentChanged, page, [](int i) {
        Config::shell().setValue(QStringLiteral("Taskbar/alignment"),
                                 i == 1 ? QStringLiteral("left") : QStringLiteral("center"));
    });
    align->setControl(seg);
    page->column()->addWidget(align);

    auto *language = new SettingRow(QStringLiteral("languages"), Text::tr("Show input language in tray"),
                                    Text::tr("Switch with Super + Space"), page);
    QObject::connect(language->addToggle(shell.value(QStringLiteral("Taskbar/showLanguage"), true).toBool()),
                     &QAbstractButton::toggled, page,
                     [](bool on) { Config::shell().setValue(QStringLiteral("Taskbar/showLanguage"), on); });
    page->column()->addWidget(language);
    page->column()->addWidget(note(QCoreApplication::translate("Personalization",
                                       "Pin and unpin apps by right-clicking them on the taskbar or in Start."),
                                   page));
    return page;
}

SettingsPage *fontsPage(QWidget *parent)
{
    auto *page = new SettingsPage(Text::tr("Fonts"), parent);
    const Ui::Theme &t = Ui::Theme::current();
    struct Sample
    {
        QString name;
        QString role;
        QFont font;
    };
    const QList<Sample> samples = {
        {QStringLiteral("IBM Plex Sans"), Text::tr("Interface and text"), Ui::bodyFont(22)},
        {QStringLiteral("IBM Plex Sans"), Text::tr("Titles"), Ui::headingFont(26)},
        {QStringLiteral("IBM Plex Mono"), Text::tr("Terminal and code"), Ui::monoFont(20)},
    };
    for (const Sample &sample : samples) {
        auto *card = new HeroCard(page);
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(26, 20, 26, 22);
        auto *name = label(sample.name + QStringLiteral(" · ") + sample.role, card);
        v->addWidget(name);
        auto *text = new QLabel(Text::tr("The quick brown fox jumps over the lazy dog"), card);
        text->setFont(sample.font);
        text->setWordWrap(true);
        QPalette palette = text->palette();
        palette.setColor(QPalette::WindowText, t.text);
        text->setPalette(palette);
        v->addWidget(text);
        page->column()->addWidget(card);
    }
    page->column()->addWidget(note(Text::tr("%n font families installed.", nullptr,
                                                               int(QFontDatabase::families().size())),
                                   page));
    return page;
}

} // namespace

SettingsPage *createPersonalizationPage(QWidget *parent)
{
    auto *page = new SettingsPage(Text::tr("Personalization"), parent);
    page->addKeywords({QStringLiteral("theme"), QStringLiteral("dark"), QStringLiteral("accent"),
                       QStringLiteral("wallpaper"), Text::tr("Theme"),
                       Text::tr("Accent color")});
    // Hero: preview, theme cards and accent swatches.
    auto *hero = new HeroCard(page);
    auto *heroLayout = new QHBoxLayout(hero);
    heroLayout->setContentsMargins(22, 22, 22, 22);
    heroLayout->setSpacing(24);
    heroLayout->addWidget(new DesktopPreview(hero), 0, Qt::AlignTop);
    auto *choices = new QVBoxLayout;
    choices->setSpacing(10);
    choices->addWidget(label(Text::tr("Theme"), hero));
    // Theme cards and swatches are auto-exclusive: each set in its own parent.
    auto *themeBox = new QWidget(hero);
    auto *themes = new QHBoxLayout(themeBox);
    themes->setContentsMargins(0, 0, 0, 0);
    themes->setSpacing(10);
    const QString scheme = Ui::Theme::schemeSetting();
    struct ThemeChoice
    {
        QString key;
        QString name;
        QColor left;
        QColor right;
    };
    const QList<ThemeChoice> themeCards = {
        {QStringLiteral("light"), Text::tr("Light"), Ui::neutral(100), Ui::accent(300)},
        {QStringLiteral("dark"), Text::tr("Dark"), Ui::neutral(900), Ui::accent(800)},
        {QStringLiteral("auto"), Text::tr("Follow sunset"), Ui::neutral(100), Ui::neutral(900)},
    };
    for (const ThemeChoice &choice : themeCards) {
        auto *card = new ThemeCard(choice.name, choice.left, choice.right, themeBox);
        card->setChecked(choice.key == scheme);
        const QString value = choice.key;
        QObject::connect(card, &QAbstractButton::clicked, page,
                         [value] { Config::desktop().setValue(QStringLiteral("Appearance/theme"), value); });
        themes->addWidget(card);
    }
    choices->addWidget(themeBox);
    choices->addSpacing(8);
    choices->addWidget(label(Text::tr("Accent color"), hero));
    auto *swatchBox = new QWidget(hero);
    auto *swatches = new QHBoxLayout(swatchBox);
    swatches->setContentsMargins(0, 0, 0, 0);
    swatches->setSpacing(2);
    const QString accent = Ui::Theme::accentSetting();
    for (const Ui::AccentChoice &choice : Ui::accentChoices()) {
        auto *swatch = new Swatch(choice.color, swatchBox);
        swatch->setChecked(choice.key == accent);
        const QString key = choice.key;
        QObject::connect(swatch, &QAbstractButton::clicked, page,
                         [key] { Config::desktop().setValue(QStringLiteral("Appearance/accent"), key); });
        swatches->addWidget(swatch);
    }
    swatches->addStretch(1);
    choices->addWidget(swatchBox);
    heroLayout->addLayout(choices, 1);
    page->column()->addWidget(hero);
    page->column()->addSpacing(6);

    const QSettings desktop = Config::desktop();
    const QSettings shell = Config::shell();

    auto *background = new SettingRow(QStringLiteral("image"), Text::tr("Background"),
                                      Text::tr("Wallpaper and how it fills the screen"), page);
    const QString mode = desktop.value(QStringLiteral("Background/mode"), QStringLiteral("organic")).toString();
    background->setLink(mode == QLatin1String("solid")     ? Text::tr("Solid color")
                        : mode == QLatin1String("picture") ? Text::tr("Picture")
                                                           : Text::tr("Organic shapes"));
    QObject::connect(background, &SettingRow::clicked, page, [page] { Q_EMIT page->subPageRequested(backgroundPage(nullptr)); });
    page->column()->addWidget(background);

    auto *taskbar = new SettingRow(QStringLiteral("panel-bottom"), Text::tr("Taskbar"),
                                   Text::tr("Pinned apps, running indicators, tray icons"), page);
    taskbar->setLink(shell.value(QStringLiteral("Taskbar/alignment")).toString() == QLatin1String("left")
                         ? Text::tr("Left")
                         : Text::tr("Centered"));
    QObject::connect(taskbar, &SettingRow::clicked, page, [page] { Q_EMIT page->subPageRequested(taskbarPage(nullptr)); });
    page->column()->addWidget(taskbar);

    auto *start = new SettingRow(QStringLiteral("layout-grid"), Text::tr("Start layout"),
                                 Text::tr("What opens first when you press Start"), page);
    const QStringList layouts = {QStringLiteral("pinned"), QStringLiteral("categories"), QStringLiteral("search")};
    auto *layoutSeg = new Segmented({Text::tr("Pinned"),
                                     Text::tr("Categories"),
                                     Text::tr("Search")});
    layoutSeg->setCurrentIndex(qMax(0, int(layouts.indexOf(shell.value(QStringLiteral("Start/layout")).toString()))));
    QObject::connect(layoutSeg, &Segmented::currentChanged, page,
                     [layouts](int i) { Config::shell().setValue(QStringLiteral("Start/layout"), layouts.at(i)); });
    start->setControl(layoutSeg);
    page->column()->addWidget(start);

    auto *language = new SettingRow(QStringLiteral("languages"), Text::tr("Show input language in tray"),
                                    Text::tr("Switch with Super + Space"), page);
    QObject::connect(language->addToggle(shell.value(QStringLiteral("Taskbar/showLanguage"), true).toBool()),
                     &QAbstractButton::toggled, page,
                     [](bool on) { Config::shell().setValue(QStringLiteral("Taskbar/showLanguage"), on); });
    page->column()->addWidget(language);

    auto *transparency = new SettingRow(QStringLiteral("layers"), Text::tr("Transparency effects"),
                                        Text::tr("See-through taskbar"), page);
    QObject::connect(transparency->addToggle(desktop.value(QStringLiteral("Appearance/transparency"), true).toBool()),
                     &QAbstractButton::toggled, page,
                     [](bool on) { Config::desktop().setValue(QStringLiteral("Appearance/transparency"), on); });
    page->column()->addWidget(transparency);

    auto *fonts = new SettingRow(QStringLiteral("type"), Text::tr("Fonts"),
                                 QStringLiteral("IBM Plex Sans · IBM Plex Mono"), page);
    fonts->setLink(Text::tr("%n installed", nullptr,
                                               int(QFontDatabase::families().size())));
    QObject::connect(fonts, &SettingRow::clicked, page, [page] { Q_EMIT page->subPageRequested(fontsPage(nullptr)); });
    page->column()->addWidget(fonts);
    return page;
}
