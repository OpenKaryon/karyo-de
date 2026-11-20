// SPDX-License-Identifier: GPL-3.0-or-later
// Taskbar controls of KaryonShell.dc.html.
#pragma once

#include <karyo/ui/appstyle.h>

#include <QAbstractButton>
#include <QTime>

// Shared hover/press background; "highlighted" marks an open flyout or a focused app.
class ShellButton : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ShellButton(QWidget *parent = nullptr);

    void setHighlighted(bool highlighted);
    bool isHighlighted() const { return m_highlighted; }
    // Tooltip shown above the button in the Karyo style.
    void setHint(const QString &title, const QString &subtitle = {});

protected:
    void paintBackground(QPainter &painter, qreal radius);
    bool event(QEvent *event) override;

private:
    bool m_highlighted = false;
    QString m_hintTitle;
    QString m_hintSubtitle;
};

// 52x52 tile, radius 16, with the Karyo mark.
class StartButton : public ShellButton
{
    Q_OBJECT
public:
    explicit StartButton(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
};

// 52x52 tile, radius 16, with a 22px glyph (Search).
class GlyphButton : public ShellButton
{
    Q_OBJECT
public:
    GlyphButton(const QString &glyph, int glyphSize, QSize size, qreal radius,
                QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QString m_glyph;
    int m_glyphSize;
    qreal m_radius;
};

// A pinned or running app: 36px tile in a 52px slot, plus the running indicator.
class DockButton : public ShellButton
{
    Q_OBJECT
public:
    enum class State { Idle, Running, Focused };

    explicit DockButton(const AppStyle &style, QWidget *parent = nullptr);
    void setState(State state);
    State state() const { return m_state; }

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    AppStyle m_style;
    State m_state = State::Idle;
};

// Input language pill: EN / RU.
class LanguageButton : public ShellButton
{
    Q_OBJECT
public:
    explicit LanguageButton(QWidget *parent = nullptr);
    void setCode(const QString &code);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QString m_code;
};

// Wi-Fi + volume pill that opens Quick settings.
class QuickButton : public ShellButton
{
    Q_OBJECT
public:
    explicit QuickButton(QWidget *parent = nullptr);
    void setNetworkGlyph(const QString &glyph, bool connected);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    QString m_networkGlyph = QStringLiteral("wifi");
    bool m_connected = true;
};

// Time over date, plus the notification-count badge.
class ClockButton : public ShellButton
{
    Q_OBJECT
public:
    explicit ClockButton(QWidget *parent = nullptr);
    void setNotificationCount(int count);
protected:
    void paintEvent(QPaintEvent *event) override;
private:
    void tick();
    void updateWidth();
    QString m_time;
    QString m_date;
    int m_count = 0;
};
