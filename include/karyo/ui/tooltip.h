// SPDX-License-Identifier: GPL-3.0-or-later
// The dark Karyo tooltip: a 16px-radius neutral-900 card with a title and
// an optional status line, placed above its anchor.
#pragma once

#include <QPointer>
#include <QWidget>

namespace Karyo::Ui {

class ToolTip : public QWidget
{
    Q_OBJECT
public:
    // Shows the shared tooltip centered above anchor, gap pixels away.
    static void showAbove(QWidget *anchor, const QString &title, const QString &subtitle = {},
                          int gap = 10);
    static void hideFor(QWidget *anchor);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    explicit ToolTip(QWidget *parent);
    void setText(const QString &title, const QString &subtitle);

    QString m_title;
    QString m_subtitle;
    QPointer<QWidget> m_anchor;
};

} // namespace Karyo::Ui
