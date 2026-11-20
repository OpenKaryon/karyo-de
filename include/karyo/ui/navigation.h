// SPDX-License-Identifier: GPL-3.0-or-later
// Sidebar navigation and the inset content panel of the Karyo apps
// (System Monitor, Settings, App Center, Files).
#pragma once

#include <QList>
#include <QWidget>

namespace Karyo::Ui {

// Vertical list of pill rows: glyph, label, optional badge or trailing text.
// The selected row uses --k-sel with an accent glyph.
class NavList : public QWidget
{
    Q_OBJECT
public:
    explicit NavList(QWidget *parent = nullptr);

    int addItem(const QString &glyph, const QString &label);
    void clear();
    void addSeparator();
    void addHeading(const QString &text);
    void setBadge(int index, const QString &badge);
    void setTrailing(int index, const QString &text);
    void setIndent(int index, int level);
    void setExpander(int index, bool expandable, bool expanded);
    void setItemVisible(int index, bool visible);
    bool isItemVisible(int index) const;
    void setCurrentIndex(int index);
    int currentIndex() const { return m_current; }
    void setRowHeight(int height);
    void setTextSize(qreal px) { m_textSize = px; update(); }
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    void currentChanged(int index);
    void activated(int index);
    void expanderToggled(int index);
    void contextMenuRequested(int index, const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct Row
    {
        enum Kind { Item, Separator, Heading } kind = Item;
        QString glyph;
        QString label;
        QString badge;
        QString trailing;
        int indent = 0;
        bool expandable = false;
        bool expanded = false;
        bool visible = true;
    };
    QList<int> layoutRows() const;  // y of each row, -1 when hidden
    int rowHeight(const Row &row) const;
    int rowAt(const QPoint &pos) const;

    QList<Row> m_rows;
    int m_current = -1;
    int m_hover = -1;
    int m_rowHeight = 36;
    qreal m_textSize = 13;
};

// The rounded --k-win panel that holds an app's content beside its sidebar.
class InsetPanel : public QWidget
{
    Q_OBJECT
public:
    explicit InsetPanel(QWidget *parent = nullptr);
    void setRadius(qreal radius) { m_radius = radius; update(); }
    void setFill(const QColor &color) { m_fill = color; update(); }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    qreal m_radius = 28;
    QColor m_fill;
};

} // namespace Karyo::Ui
