// SPDX-License-Identifier: GPL-3.0-or-later
#include "layersurface.h"

#include <LayerShellQt/Window>

#include <QMargins>
#include <QScreen>
#include <QWidget>
#include <QWindow>

namespace LayerSurface {

void configure(QWidget *widget, QScreen *screen, Layer layer, Anchors anchors, int exclusiveZone,
               bool keyboardOnDemand)
{
    // The platform window must exist, but not be mapped, when the role is assigned.
    widget->winId();
    QWindow *window = widget->windowHandle();
    window->setScreen(screen);

    auto *surface = LayerShellQt::Window::get(window);
    surface->setScope(QStringLiteral("karyo-shell"));
    surface->setLayer(LayerShellQt::Window::Layer(layer));

    LayerShellQt::Window::Anchors edges;
    if (anchors & AnchorTop)
        edges |= LayerShellQt::Window::AnchorTop;
    if (anchors & AnchorBottom)
        edges |= LayerShellQt::Window::AnchorBottom;
    if (anchors & AnchorLeft)
        edges |= LayerShellQt::Window::AnchorLeft;
    if (anchors & AnchorRight)
        edges |= LayerShellQt::Window::AnchorRight;
    surface->setAnchors(edges);
    surface->setExclusiveZone(exclusiveZone);
    surface->setKeyboardInteractivity(keyboardOnDemand
                                          ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                                          : LayerShellQt::Window::KeyboardInteractivityNone);
    surface->setScreenConfiguration(LayerShellQt::Window::ScreenFromQWindow);
}

void setMargins(QWidget *widget, const QMargins &margins)
{
    LayerShellQt::Window::get(widget->windowHandle())->setMargins(margins);
}

void setKeyboard(QWidget *widget, Keyboard keyboard)
{
    LayerShellQt::Window::get(widget->windowHandle())
        ->setKeyboardInteractivity(keyboard == KeyboardExclusive ? LayerShellQt::Window::KeyboardInteractivityExclusive
                                   : keyboard == KeyboardOnDemand ? LayerShellQt::Window::KeyboardInteractivityOnDemand
                                                                  : LayerShellQt::Window::KeyboardInteractivityNone);
}

} // namespace LayerSurface
