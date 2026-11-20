// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/appstyle.h"

#include "karyo/ui/desktopentry.h"
#include "karyo/ui/icons.h"
#include "karyo/ui/paint.h"
#include "karyo/ui/theme.h"

#include <QFile>
#include <QPainter>

namespace Ui = Karyo::Ui;

namespace {

struct Role
{
    QStringList ids;  // desktop ids without .desktop
    QStringList categories;
    const char *glyph;
    const char *monogram;
    QColor tile;
    QColor ink;
};

// The APPS table of KaryonShell.dc.html, keyed by the Debian apps that fill each role.
const QList<Role> &roles()
{
    static const QList<Role> table = {
        {{QStringLiteral("pcmanfm-qt"), QStringLiteral("openkaryon-files")},
         {QStringLiteral("FileManager")}, "folder", nullptr, Ui::accent(300), Ui::accent(800)},
        {{QStringLiteral("firefox-esr"), QStringLiteral("firefox")},
         {QStringLiteral("WebBrowser")}, "globe", nullptr, Ui::sage(300), Ui::sage(800)},
        {{QStringLiteral("karyo-terminal"), QStringLiteral("qterminal"), QStringLiteral("foot"), QStringLiteral("openkaryon-terminal")},
         {QStringLiteral("TerminalEmulator")}, "terminal", nullptr, Ui::neutral(800), Ui::sage(200)},
        {{QStringLiteral("featherpad"), QStringLiteral("openkaryon-editor")},
         {QStringLiteral("TextEditor")}, "file-pen-line", nullptr, Ui::accent(200), Ui::accent(700)},
        {{QStringLiteral("rstudio")}, {}, nullptr, "R", Ui::sage(200), Ui::sage(800)},
        {{QStringLiteral("lximage-qt"), QStringLiteral("openkaryon-photos")}, {}, "image", nullptr, Ui::accent(200),
         Ui::accent(800)},
        {{QStringLiteral("openkaryon-player")}, {}, "play", nullptr, Ui::accent(600), Ui::neutral(100)},
        {{QStringLiteral("openkaryon-snip")}, {}, "scissors", nullptr, Ui::sage(600), Ui::neutral(100)},
        {{QStringLiteral("openkaryon-reader")}, {}, "file-text", nullptr, Ui::accent(700), Ui::neutral(100)},
        {{QStringLiteral("openkaryon-calculator"), QStringLiteral("qalculate-qt")}, {}, "calculator", nullptr,
         Ui::sage(700), Ui::neutral(100)},
        {{QStringLiteral("openkaryon-clock")}, {}, "hourglass", nullptr, Ui::accent(600), Ui::neutral(100)},
        {{QStringLiteral("openkaryon-appcenter")}, {QStringLiteral("PackageManager")}, "package",
         nullptr, Ui::accent(500), Ui::neutral(100)},
        {{QStringLiteral("openkaryon-monitor"), QStringLiteral("qps")}, {QStringLiteral("Monitor")},
         "activity", nullptr, Ui::sage(600), Ui::neutral(100)},
        {{QStringLiteral("openkaryon-install")}, {}, "download", nullptr, Ui::accent(500), Ui::neutral(100)},
        {{QStringLiteral("lxqt-config"), QStringLiteral("openkaryon-settings")},
         {QStringLiteral("Settings")}, "settings", nullptr, Ui::neutral(300), Ui::neutral(800)},
    };
    return table;
}

} // namespace

AppStyle AppStyle::forEntry(const DesktopEntry &entry)
{
    QString id = entry.id;
    if (id.endsWith(QLatin1String(".desktop")))
        id.chop(8);
    for (const Role &role : roles()) {
        if (!role.ids.contains(id))
            continue;
        AppStyle style;
        style.glyph = role.glyph ? QString::fromLatin1(role.glyph) : QString();
        style.monogram = role.monogram ? QString::fromLatin1(role.monogram) : QString();
        style.tile = role.tile;
        style.ink = role.ink;
        return style;
    }
    AppStyle style;
    style.themeIcon = QIcon::fromTheme(entry.icon);
    if (style.themeIcon.isNull() && QFile::exists(entry.icon))
        style.themeIcon = QIcon(entry.icon);
    if (style.themeIcon.isNull())
        return fallback(entry.name);
    return style;
}

AppStyle AppStyle::fallback(const QString &name)
{
    // Lettered monogram tile, like the design's placeholders for third-party apps.
    static const QList<QPair<QColor, QColor>> palette = {
        {Ui::accent(200), Ui::accent(700)},
        {Ui::sage(200), Ui::sage(800)},
        {Ui::neutral(300), Ui::neutral(800)},
        {Ui::accent(300), Ui::accent(800)},
    };
    AppStyle style;
    style.monogram = QStringLiteral("?");
    for (const QChar c : name) {
        if (c.isLetterOrNumber()) {
            style.monogram = c.toUpper();
            break;
        }
    }
    const auto &colors = palette.at(int(qHash(name) % palette.size()));
    style.tile = colors.first;
    style.ink = colors.second;
    return style;
}

void AppStyle::paint(QPainter &painter, const QRectF &tile, qreal devicePixelRatio) const
{
    const qreal scale = tile.width() / 36.0;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (tile.isValid() && this->tile.isValid())
        Ui::fillRounded(painter, tile, 11 * scale, this->tile);
    if (!glyph.isEmpty()) {
        const int size = qRound(20 * scale);
        const QPixmap pixmap = Ui::icon(glyph, size, ink, devicePixelRatio);
        painter.drawPixmap(QPointF(tile.center().x() - size / 2.0, tile.center().y() - size / 2.0),
                           pixmap);
    } else if (!monogram.isEmpty()) {
        // One letter for apps; a short mono name ("fqc") for command-line tools.
        painter.setFont(monogram.size() > 1 ? Ui::monoFont((monogram.size() > 2 ? 12 : 13.5) * scale, QFont::DemiBold)
                                            : Ui::bodyFont(15 * scale, QFont::Bold));
        painter.setPen(ink);
        painter.drawText(tile, Qt::AlignCenter, monogram);
    } else {
        themeIcon.paint(&painter, tile.toRect());
    }
    painter.restore();
}
