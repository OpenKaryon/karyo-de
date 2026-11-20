// SPDX-License-Identifier: GPL-3.0-or-later
// Client-side window frame of the Karyo apps: radius 28 body, a 52-56px
// title strip that holds tabs, a title or a search field, and pill window
// controls. labwc draws no frame for app ids matching openkaryon-*.
#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QIcon>
#include <QList>
#include <QWidget>

class QHBoxLayout;
class QLineEdit;
class QVBoxLayout;

namespace Karyo::Ui {

// Icon-only pill button: toolbar actions, nav arrows, window controls.
class IconButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit IconButton(const QString &glyph, QWidget *parent = nullptr, QSize size = {36, 36},
                        int glyphSize = 18);
    void setGlyph(const QString &glyph);
    void setGlyphColor(const QColor &color) { m_color = color; update(); }
    void setHoverColor(const QColor &color) { m_hoverColor = color; update(); }
    // Close-button behavior: hover fills with the accent and the glyph turns light.
    void setDangerHover(bool danger) { m_danger = danger; }
    void setActive(bool active) { m_active = active; update(); }
    void setHint(const QString &hint);

protected:
    void paintEvent(QPaintEvent *event) override;
    bool event(QEvent *event) override;

private:
    QString m_glyph;
    QString m_hint;
    int m_glyphSize;
    QColor m_color;
    QColor m_hoverColor;
    bool m_danger = false;
    bool m_active = false;
};

// Pill button with an optional leading glyph, text and trailing chevron
// ("Sort ▾", "Open in Terminal", "+ New ▾").
class PillButton : public QAbstractButton
{
    Q_OBJECT
public:
    enum class Kind { Ghost, Secondary, Primary, Tint };
    PillButton(const QString &glyph, const QString &text, QWidget *parent = nullptr);
    void setKind(Kind kind) { m_kind = kind; update(); }
    void setGlyph(const QString &glyph)
    {
        m_glyph = glyph;
        updateGeometry();
        update();
    }
    void setChevron(bool chevron);
    void setActive(bool active) { m_active = active; update(); }
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QString m_glyph;
    Kind m_kind = Kind::Ghost;
    bool m_chevron = false;
    bool m_active = false;
};

// Tabs drawn in a title strip; the active tab takes the window body color.
class TitleTabs : public QWidget
{
    Q_OBJECT
public:
    explicit TitleTabs(QWidget *parent = nullptr);

    int addTab(const QString &glyph, const QString &title);
    void removeTab(int index);
    void setTabTitle(int index, const QString &title);
    void setTabGlyph(int index, const QString &glyph);
    void setTabDirty(int index, bool dirty);
    int count() const { return int(m_tabs.size()); }
    int currentIndex() const { return m_current; }
    void setCurrentIndex(int index);
    // Colors: the active tab's fill (window body) and the strip's ink.
    void setColors(const QColor &activeFill, const QColor &text, const QColor &muted,
                   const QColor &hover = {});
    // Accent of the active tab's glyph.
    void setGlyphColor(const QColor &color) { m_glyphColor = color; update(); }
    void setMaxTabWidth(int width) { m_maxTabWidth = width; update(); }
    void setShowNewMenu(bool show);
    QSize sizeHint() const override;

Q_SIGNALS:
    void currentChanged(int index);
    void closeRequested(int index);
    void newTabRequested();
    void newMenuRequested(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    struct Tab
    {
        QString glyph;
        QString title;
        bool dirty = false;
    };
    QRect tabRect(int index) const;
    QRect closeRect(int index) const;
    QRect plusRect() const;
    QRect menuRect() const;
    int tabWidth() const;

    QList<Tab> m_tabs;
    int m_current = -1;
    int m_hover = -1;
    int m_hoverClose = -1;
    int m_hoverExtra = 0;  // 1 plus, 2 menu
    bool m_showNewMenu = false;
    QColor m_activeFill;
    QColor m_text;
    QColor m_muted;
    QColor m_hoverFill;
    QColor m_glyphColor;
    int m_maxTabWidth = 240;
};

// Pill search field with a leading magnifier, as in the design's title strips.
class SearchField : public QWidget
{
    Q_OBJECT
public:
    explicit SearchField(const QString &placeholder, QWidget *parent = nullptr);
    QLineEdit *lineEdit() const { return m_edit; }
    void setFillColor(const QColor &color) { m_fill = color; update(); }

protected:
    void paintEvent(QPaintEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QLineEdit *m_edit;
    QColor m_fill;
};

class KaryoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit KaryoWindow(QWidget *parent = nullptr);

    // Widgets placed in the title strip go here, before the window controls.
    QHBoxLayout *titleLayout() const { return m_titleLayout; }
    QWidget *titleBar() const { return m_titleBar; }
    void setCentralWidget(QWidget *widget);
    void setTitleHeight(int height);
    // The preferred size, shrunk to fit the screen's available area.
    void resizeToFit(const QSize &preferred);
    void setBodyColor(const QColor &color);
    QColor bodyColor() const { return m_body; }
    // The title strip may differ from the body (Files: pane strip over a window body).
    void setTitleColor(const QColor &color);
    // Window controls on a dark title strip (Terminal).
    void setControlColors(const QColor &glyph, const QColor &hover);
    qreal cornerRadius() const;

protected:
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void toggleMaximized();

    QWidget *m_titleBar;
    QHBoxLayout *m_titleLayout;
    QVBoxLayout *m_layout;
    QWidget *m_central = nullptr;
    IconButton *m_maximize;
    QList<IconButton *> m_controls;
    QColor m_body;
    QColor m_title;
};

} // namespace Karyo::Ui
