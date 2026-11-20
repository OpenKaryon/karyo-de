// SPDX-License-Identifier: GPL-3.0-or-later
#include "openkaryon/ui/theme.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

namespace OpenKaryon::Ui {

namespace {

constexpr QRgb NeutralRamp[9] = {0xf9f4ed, 0xeee7db, 0xdcd3c4, 0xc0b6a5, 0xa19786,
                                 0x82796a, 0x645c50, 0x474238, 0x2e2b25};
constexpr QRgb AccentRamp[9] = {0xfff2eb, 0xffe1d0, 0xffc6a5, 0xf6a06b, 0xd67f48,
                                0xb2622d, 0x8c491a, 0x643312, 0x402310};
constexpr QRgb SageRamp[9] = {0xf0fae1, 0xe1eecc, 0xccdbb2, 0xaebf92, 0x8fa073,
                              0x728157, 0x56633f, 0x3d472b, 0x272e1b};

QColor rampStep(const QRgb (&ramp)[9], int step)
{
    const int index = qBound(1, step / 100, 9) - 1;
    return QColor::fromRgb(ramp[index]);
}

const QColor Terracotta(0xc6, 0x71, 0x39);

QSettings desktopSettings()
{
    return QSettings(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("openkaryon"),
                     QStringLiteral("desktop"));
}

// The chosen accent and the ramp made from it. Terracotta keeps the design's
// hand-tuned ramp; other accents get one mixed toward white and black.
struct AccentState
{
    QString key;
    QColor base;
    QColor ramp[9];
    bool custom = false;
};

AccentState loadAccent()
{
    AccentState state;
    state.key = Theme::accentSetting();
    state.base = Terracotta;
    for (const AccentChoice &choice : accentChoices()) {
        if (choice.key == state.key && choice.color != Terracotta) {
            state.base = choice.color;
            state.custom = true;
        }
    }
    if (state.custom) {
        const QColor white(255, 255, 255);
        const QColor black(0, 0, 0);
        const qreal tints[4] = {8, 18, 38, 68};
        for (int i = 0; i < 4; ++i)
            state.ramp[i] = mix(state.base, white, tints[i]);
        state.ramp[4] = state.base;
        const qreal shades[4] = {82, 64, 46, 30};
        for (int i = 0; i < 4; ++i)
            state.ramp[5 + i] = mix(state.base, black, shades[i]);
    }
    return state;
}

AccentState &accentState()
{
    static AccentState state = loadAccent();
    return state;
}

Theme &mutableCurrent()
{
    static Theme theme = Theme::make(Theme::configuredScheme());
    return theme;
}

// The look before the last change, for Theme::follow().
Theme &previousTheme()
{
    static Theme theme = mutableCurrent();
    return theme;
}

// Replaces the current look; colors widgets took from the old one follow.
void replaceCurrent(const Theme &theme)
{
    previousTheme() = mutableCurrent();
    mutableCurrent() = theme;
}

} // namespace

QColor mix(const QColor &a, const QColor &b, qreal pct)
{
    // CSS color-mix interpolates premultiplied sRGB components.
    const qreal p = pct / 100.0;
    const qreal q = 1.0 - p;
    const qreal outAlpha = a.alphaF() * p + b.alphaF() * q;
    if (outAlpha <= 0.0)
        return QColor(0, 0, 0, 0);
    auto channel = [&](qreal ca, qreal cb) {
        return (ca * a.alphaF() * p + cb * b.alphaF() * q) / outAlpha;
    };
    QColor result;
    result.setRgbF(float(channel(a.redF(), b.redF())), float(channel(a.greenF(), b.greenF())),
                   float(channel(a.blueF(), b.blueF())), float(outAlpha));
    return result;
}

QColor alpha(const QColor &c, qreal pct)
{
    QColor result = c;
    result.setAlphaF(float(c.alphaF() * pct / 100.0));
    return result;
}

QColor neutral(int step) { return rampStep(NeutralRamp, step); }

QColor accent(int step)
{
    const AccentState &state = accentState();
    if (!state.custom)
        return rampStep(AccentRamp, step);
    return state.ramp[qBound(1, step / 100, 9) - 1];
}

QColor sage(int step) { return rampStep(SageRamp, step); }

QList<AccentChoice> accentChoices()
{
    return {
        {QStringLiteral("terracotta"), Terracotta},
        {QStringLiteral("sage"), QColor(0x7a, 0x8a, 0x5e)},
        {QStringLiteral("clay"), rampStep(AccentRamp, 700)},
        {QStringLiteral("moss"), rampStep(SageRamp, 700)},
        {QStringLiteral("stone"), rampStep(NeutralRamp, 700)},
        {QStringLiteral("apricot"), rampStep(AccentRamp, 400)},
    };
}

Theme Theme::make(ColorScheme scheme)
{
    Theme t;
    t.scheme = scheme;
    t.accent = accentState().base;
    t.accent2 = QColor(0x7a, 0x8a, 0x5e);
    const QColor black(0, 0, 0);

    if (scheme == ColorScheme::Light) {
        t.bg = QColor(0xf5, 0xea, 0xd8);
        t.text = QColor(0x20, 0x1e, 0x1d);
        t.desk = t.bg;
        t.win = neutral(100);
        t.pane = t.bg;
        t.card = neutral(200);
        t.glass = alpha(neutral(100), 74);
        t.glassStrong = alpha(neutral(100), 90);
        t.edge = alpha(t.text, 10);
        t.hover = alpha(t.text, 7);
        t.press = alpha(t.text, 13);
        t.sel = alpha(t.accent, 16);
        t.selInk = ::OpenKaryon::Ui::accent(800);
        t.muted = alpha(t.text, 64);
        t.ink1 = ::OpenKaryon::Ui::accent(700);
        t.ink2 = sage(700);
        t.tint1 = alpha(t.accent, 14);
        t.tint2 = alpha(t.accent2, 20);
        t.blobA = ::OpenKaryon::Ui::accent(300);
        t.blobB = sage(300);
        t.blobC = ::OpenKaryon::Ui::accent(200);
        t.blobD = sage(200);
        t.blobE = ::OpenKaryon::Ui::accent(500);
    } else {
        t.bg = mix(neutral(900), black, 62);
        t.text = neutral(200);
        t.desk = t.bg;
        t.win = neutral(900);
        t.pane = mix(neutral(900), black, 78);
        t.card = mix(neutral(800), neutral(900), 62);
        t.glass = alpha(neutral(900), 80);
        t.glassStrong = alpha(neutral(900), 92);
        t.edge = alpha(neutral(200), 11);
        t.hover = alpha(neutral(200), 8);
        t.press = alpha(neutral(200), 14);
        t.sel = alpha(t.accent, 24);
        t.selInk = ::OpenKaryon::Ui::accent(300);
        t.muted = alpha(neutral(200), 64);
        t.ink1 = ::OpenKaryon::Ui::accent(400);
        t.ink2 = sage(400);
        t.tint1 = alpha(t.accent, 22);
        t.tint2 = alpha(t.accent2, 26);
        t.blobA = ::OpenKaryon::Ui::accent(800);
        t.blobB = sage(800);
        t.blobC = ::OpenKaryon::Ui::accent(900);
        t.blobD = sage(900);
        t.blobE = ::OpenKaryon::Ui::accent(600);
    }
    t.term = mix(neutral(900), black, 82);
    t.term2 = neutral(900);
    return t;
}

const Theme &Theme::current() { return mutableCurrent(); }

QString Theme::schemeSetting()
{
    const QString value = desktopSettings().value(QStringLiteral("Appearance/theme")).toString();
    return value == QLatin1String("dark") || value == QLatin1String("auto") ? value : QStringLiteral("light");
}

QString Theme::accentSetting()
{
    return desktopSettings().value(QStringLiteral("Appearance/accent"), QStringLiteral("terracotta")).toString();
}

ColorScheme Theme::configuredScheme()
{
    const QString value = schemeSetting();
    if (value == QLatin1String("auto")) {
        // Follow sunset without a location: dark from 19:00 to 7:00.
        const int hour = QTime::currentTime().hour();
        return hour >= 7 && hour < 19 ? ColorScheme::Light : ColorScheme::Dark;
    }
    return value == QLatin1String("dark") ? ColorScheme::Dark : ColorScheme::Light;
}

void Theme::setScheme(ColorScheme scheme)
{
    if (mutableCurrent().scheme == scheme)
        return;
    replaceCurrent(Theme::make(scheme));
    Q_EMIT ThemeNotifier::instance()->changed();
}

QColor Theme::follow(const QColor &color)
{
    // Most-used tokens first: in the light scheme pane, bg and desk are one color.
    static const QColor Theme::*const tokens[] = {
        &Theme::win, &Theme::pane, &Theme::card, &Theme::text, &Theme::muted, &Theme::edge, &Theme::hover,
        &Theme::press, &Theme::sel, &Theme::selInk, &Theme::ink1, &Theme::ink2, &Theme::tint1, &Theme::tint2,
        &Theme::glass, &Theme::glassStrong, &Theme::accent, &Theme::accent2, &Theme::term, &Theme::term2,
        &Theme::bg, &Theme::desk, &Theme::blobA, &Theme::blobB, &Theme::blobC, &Theme::blobD, &Theme::blobE,
    };
    if (!color.isValid())
        return color;
    const Theme &before = previousTheme();
    for (const QColor Theme::*token : tokens) {
        if ((before.*token).isValid() && (before.*token).rgba() == color.rgba())
            return mutableCurrent().*token;
    }
    return color;
}

QFont bodyFont(qreal px, int weight)
{
    QFont font;
    font.setFamilies({QStringLiteral("IBM Plex Sans"), QStringLiteral("Noto Sans"),
                      QStringLiteral("DejaVu Sans")});
    font.setPixelSize(qRound(px));
    font.setWeight(QFont::Weight(weight));
    return font;
}

QFont headingFont(qreal px)
{
    QFont font;
    font.setFamilies({QStringLiteral("IBM Plex Sans"),
                      QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans")});
    font.setWeight(QFont::DemiBold);
    font.setPixelSize(qRound(px));
    return font;
}

QFont monoFont(qreal px, int weight)
{
    QFont font;
    font.setFamilies({QStringLiteral("IBM Plex Mono"), QStringLiteral("DejaVu Sans Mono")});
    font.setStyleHint(QFont::Monospace);
    font.setPixelSize(qRound(px));
    font.setWeight(QFont::Weight(weight));
    return font;
}

void Theme::followConfiguration()
{
    static bool following = false;
    if (following)
        return;
    following = true;
    ThemeNotifier *notifier = ThemeNotifier::instance();
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                        + QStringLiteral("/openkaryon");
    QDir().mkpath(dir);

    auto reload = [] {
        const QString key = Theme::accentSetting();
        const ColorScheme scheme = Theme::configuredScheme();
        if (key == accentState().key && scheme == mutableCurrent().scheme)
            return;
        accentState() = loadAccent();
        replaceCurrent(Theme::make(scheme));
        Q_EMIT ThemeNotifier::instance()->changed();
    };

    // QSettings replaces files on save, which drops a file watch: the folder
    // is watched, and each file's time tells which one changed.
    reload();
    auto *watcher = new QFileSystemWatcher({dir}, notifier);
    auto *stamps = new QHash<QString, QDateTime>;
    auto scan = [dir, stamps, notifier, reload](bool announce) {
        const QFileInfoList files = QDir(dir).entryInfoList({QStringLiteral("*.conf")}, QDir::Files);
        for (const QFileInfo &file : files) {
            const QDateTime stamp = file.lastModified();
            if (stamps->value(file.baseName()) == stamp)
                continue;
            stamps->insert(file.baseName(), stamp);
            if (!announce)
                continue;
            if (file.baseName() == QLatin1String("desktop"))
                reload();
            Q_EMIT notifier->configurationChanged(file.baseName());
        }
    };
    scan(false);
    QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, notifier, [scan] { scan(true); });

    auto *minute = new QTimer(notifier);
    QObject::connect(minute, &QTimer::timeout, notifier, reload);
    minute->start(60 * 1000);
}

ThemeNotifier *ThemeNotifier::instance()
{
    static ThemeNotifier notifier;
    return &notifier;
}

} // namespace OpenKaryon::Ui
