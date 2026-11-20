// SPDX-License-Identifier: GPL-3.0-or-later
// The Karyo Qt style: Fusion's geometry, painted with the Organic shapes and
// tokens (pills, soft rounded selections, thin scrollbars, accent controls).
// Our apps install it directly; other Qt apps get it as the "Karyo" style
// plugin through QT_STYLE_OVERRIDE.
#pragma once

#include <QPalette>
#include <QProxyStyle>

namespace Karyo::Ui {

// QPalette roles mapped from the current Theme.
QPalette palette();

class KaryoStyle : public QProxyStyle
{
    Q_OBJECT
public:
    KaryoStyle();

    QPalette standardPalette() const override;
    void polish(QApplication *app) override;
    void polish(QWidget *widget) override;
    void unpolish(QWidget *widget) override;

    int pixelMetric(PixelMetric metric, const QStyleOption *option = nullptr,
                    const QWidget *widget = nullptr) const override;
    int styleHint(StyleHint hint, const QStyleOption *option = nullptr,
                  const QWidget *widget = nullptr,
                  QStyleHintReturn *returnData = nullptr) const override;
    QSize sizeFromContents(ContentsType type, const QStyleOption *option, const QSize &size,
                           const QWidget *widget) const override;
    QRect subElementRect(SubElement element, const QStyleOption *option,
                         const QWidget *widget) const override;
    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget) const override;

    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter,
                       const QWidget *widget = nullptr) const override;
    void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
                     const QWidget *widget = nullptr) const override;
    void drawComplexControl(ComplexControl control, const QStyleOptionComplex *option,
                            QPainter *painter, const QWidget *widget = nullptr) const override;
};

// Installs KaryoStyle, its palette and the body font for this application.
void applyStyle();

} // namespace Karyo::Ui
