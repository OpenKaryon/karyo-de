// SPDX-License-Identifier: GPL-3.0-or-later
#include <openkaryon/ui/paint.h>
#include <openkaryon/ui/theme.h>
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
        OpenKaryon::Ui::fillPill(painter, image.rect(),
            OpenKaryon::Ui::Theme::make(OpenKaryon::Ui::ColorScheme::Dark).win);
    }
    return image.pixelColor(40, 20).alpha() == 255 ? 0 : 1;
}
