// SPDX-License-Identifier: GPL-3.0-or-later
// The Karyo window frame for Qt applications we do not build (Calamares).
// labwc can round only the title bar of the frames it draws, so these
// windows draw their own, like KaryoWindow: a title strip with the window
// controls, all four corners rounded, resizing from the edges.
//
// The Karyo style plugin installs it when KARYO_CLIENT_FRAME=1. labwc
// must not decorate the application's windows (a windowRule with
// serverDecoration="no" for its app id).
#pragma once

class QApplication;

namespace Karyo::Ui {

// Frames every window and dialog the application creates from now on;
// dialogs get only the close button. Menus, popups and tool windows are left
// as they are.
void installClientFrames(QApplication *app);

} // namespace Karyo::Ui
