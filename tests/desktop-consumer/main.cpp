// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/appstyle.h>
#include <karyo/ui/desktopentry.h>
#include <karyo/ui/icons.h>
#include <QApplication>
#include <QImage>
#include <QPainter>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    if (!Karyo::Ui::hasIcon(QStringLiteral("folder")))
        return 1;
    const auto glyph = Karyo::Ui::icon(QStringLiteral("folder"), 24, Qt::red, 2);
    if (glyph.isNull() || glyph.size() != QSize(48, 48))
        return 2;
    DesktopEntry entry;
    entry.id = QStringLiteral("openkaryon-files.desktop");
    entry.name = QStringLiteral("Files");
    QImage tile(36, 36, QImage::Format_ARGB32_Premultiplied);
    tile.fill(Qt::transparent);
    QPainter painter(&tile);
    AppStyle::forEntry(entry).paint(painter, tile.rect(), 1);
    painter.end();
    return tile.pixelColor(18, 18).alpha() > 0 ? 0 : 3;
}
