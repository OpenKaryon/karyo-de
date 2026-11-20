// SPDX-License-Identifier: GPL-3.0-or-later
#include "karyo/ui/menu.h"

#include "karyo/ui/theme.h"

#include <QMenu>

namespace Karyo::Ui {

namespace {

QString css(const QColor &c)
{
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(c.alphaF(), 0, 'f', 3);
}

} // namespace

void styleMenu(QMenu *menu)
{
    const Theme &t = Theme::current();
    menu->setAttribute(Qt::WA_TranslucentBackground);
    menu->setWindowFlag(Qt::FramelessWindowHint);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint);
    menu->setFont(bodyFont(13.5));
    // Without blur the strong glass is drawn opaque so text stays readable.
    QColor background = t.glassStrong;
    background.setAlpha(255);
    menu->setStyleSheet(QStringLiteral(
        "QMenu { background: %1; border: 1px solid %2; border-radius: 20px; padding: 8px; color: %3; }"
        "QMenu::item { padding: 8px 18px 8px 14px; border-radius: 12px; margin: 1px 0; }"
        "QMenu::item:selected { background: %4; }"
        "QMenu::item:disabled { color: %5; }"
        "QMenu::icon { padding-left: 10px; }"
        "QMenu::separator { height: 1px; background: %2; margin: 6px 10px; }"
        "QMenu::indicator { width: 16px; height: 16px; padding-left: 8px; }")
                            .arg(css(background), css(t.edge), css(t.text), css(t.hover),
                                 css(alpha(t.text, 45))));
}

} // namespace Karyo::Ui
