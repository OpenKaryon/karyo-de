// SPDX-License-Identifier: GPL-3.0-or-later
// Karyo Terminal (design 1f): dark window, tabs in the title strip, SSH
// hosts from ~/.ssh/config in the new-tab menu.
#pragma once

#include <karyo/ui/window.h>

#include <QStringList>

class QStackedWidget;
class QTermWidget;

class TerminalWindow : public Karyo::Ui::KaryoWindow
{
    Q_OBJECT
public:
    explicit TerminalWindow(QWidget *parent = nullptr);

    // program empty: the user's shell. title empty: follow the shell's title.
    void newTab(const QString &directory, const QString &program = {},
                const QStringList &args = {}, const QString &glyph = QStringLiteral("terminal"),
                const QString &title = {});

private:
    QTermWidget *current() const;
    void closeTab(int index);
    void showNewMenu(const QPoint &globalPos);
    void showContextMenu(QTermWidget *term, const QPoint &pos);
    void zoom(int steps);
    static QStringList sshHosts();
    static QString colorSchemePath();

    Karyo::Ui::TitleTabs *m_tabs;
    QStackedWidget *m_stack;
    int m_fontPixels = 14;
};
