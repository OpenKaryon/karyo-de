// SPDX-License-Identifier: GPL-3.0-or-later
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <QApplication>
#include <QImage>
#include <QPainter>

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    QImage image(80, 40, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        Karyo::Ui::fillPill(painter, image.rect(),
            Karyo::Ui::Theme::make(Karyo::Ui::ColorScheme::Dark).win);
    }
    return image.pixelColor(40, 20).alpha() == 255 ? 0 : 1;
}
