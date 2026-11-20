// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo design tokens: the Organic base palette and the semantic --k-* tokens
// from docs/design/karyo-linux, for light and dark schemes.
#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QObject>

namespace Karyo::Ui {

enum class ColorScheme { Light, Dark };

// Mixes a over b like CSS color-mix(in srgb, a pct%, b).
QColor mix(const QColor &a, const QColor &b, qreal pct);
// CSS color-mix(in srgb, c pct%, transparent).
QColor alpha(const QColor &c, qreal pct);

// Base ramps, steps 100..900. The accent ramp follows the chosen accent color.
QColor neutral(int step);
QColor accent(int step);
QColor sage(int step);

// Accent colors offered in Settings (KaryonSettings.dc.html): terracotta
// (the default), sage, clay, moss, stone and apricot.
struct AccentChoice
{
    QString key;
    QColor color;
};
QList<AccentChoice> accentChoices();

struct Theme
{
    ColorScheme scheme = ColorScheme::Light;

    QColor bg;       // --color-bg
    QColor text;     // --color-text
    QColor accent;   // --color-accent
    QColor accent2;  // --color-accent-2

    QColor desk;
    QColor win;
    QColor pane;
    QColor card;
    QColor glass;
    QColor glassStrong;
    QColor edge;
    QColor hover;
    QColor press;
    QColor sel;
    QColor selInk;
    QColor muted;
    QColor ink1;
    QColor ink2;
    QColor tint1;
    QColor tint2;
    QColor term;
    QColor term2;
    QColor blobA;
    QColor blobB;
    QColor blobC;
    QColor blobD;
    QColor blobE;

    static Theme make(ColorScheme scheme);
    static const Theme &current();
    // Appearance/theme in openkaryon/desktop.conf: light, dark or auto
    // (follow sunset: dark from 19:00 to 7:00).
    static QString schemeSetting();
    // Appearance/accent: a key of accentChoices().
    static QString accentSetting();
    static ColorScheme configuredScheme();
    static void setScheme(ColorScheme scheme);
    // Rereads desktop.conf now, whenever it changes and every minute (for
    // auto), and emits ThemeNotifier::changed when the look changes.
    // applyStyle() turns it on for every Karyo app.
    static void followConfiguration();
    // A color taken from the look before the last change, in the current
    // look: the same token's new color; other colors stay. Widgets that keep
    // colors call it on ThemeNotifier::changed.
    static QColor follow(const QColor &color);
};

// Pixel sizes match the CSS pixels of the design at 1x.
QFont bodyFont(qreal px, int weight = QFont::Normal);
QFont headingFont(qreal px);
QFont monoFont(qreal px, int weight = QFont::Normal);

class ThemeNotifier : public QObject
{
    Q_OBJECT
public:
    static ThemeNotifier *instance();
Q_SIGNALS:
    void changed();
    // A file in ~/.config/openkaryon changed: "desktop", "shell"...
    // Emitted once followConfiguration() runs.
    void configurationChanged(const QString &name);
};

} // namespace Karyo::Ui
