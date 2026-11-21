// SPDX-License-Identifier: GPL-3.0-or-later
// Building blocks of Settings (KaryonSettings.dc.html): the page title,
// setting rows (icon, title, description and a control at the right), the
// toggle switch, the segmented control and fact cards.
#pragma once

#include <QAbstractButton>
#include <QLabel>
#include <QList>
#include <QPair>
#include <QWidget>

#include <functional>

class QComboBox;
class QHBoxLayout;
class QVBoxLayout;

// 46x26 pill; on: accent with a light knob at the right.
class ToggleSwitch : public QAbstractButton
{
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget *parent = nullptr);
    QSize sizeHint() const override { return {46, 26}; }

protected:
    void paintEvent(QPaintEvent *event) override;
};

// Pills in an outlined pill: "Pinned | Categories | Search".
class Segmented : public QWidget
{
    Q_OBJECT
public:
    Segmented(const QStringList &options, QWidget *parent = nullptr);
    void setCurrentIndex(int index);
    int currentIndex() const { return m_current; }
    QSize sizeHint() const override;

Q_SIGNALS:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QRect optionRect(int index) const;

    QStringList m_options;
    int m_current = 0;
    int m_hover = -1;
};

// A --k-card row, radius 22, at least 68px high: a glyph in a light circle,
// title and description, then whatever goes at the right. Rows that lead to
// a sub-page show a value and a chevron and are clickable.
class SettingRow : public QWidget
{
    Q_OBJECT
public:
    SettingRow(const QString &glyph, const QString &title, const QString &description,
               QWidget *parent = nullptr);

    // Right side: a widget (toggle, segmented, combo, button)...
    void setControl(QWidget *control);
    // ...or a value with a chevron; the row becomes clickable.
    void setLink(const QString &value);
    void setValue(const QString &value);
    void setDescription(const QString &description);
    // A toggle with its "On"/"Off" label; returns it for connecting.
    ToggleSwitch *addToggle(bool on);

Q_SIGNALS:
    void clicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;

private:
    QString m_glyph;
    QLabel *m_description;
    QHBoxLayout *m_right;
    QLabel *m_value = nullptr;
    bool m_link = false;
    bool m_hover = false;
};

// Rows of label and value on a card: About, network details.
class FactsCard : public QWidget
{
    Q_OBJECT
public:
    explicit FactsCard(QWidget *parent = nullptr);
    void setFacts(const QList<QPair<QString, QString>> &facts);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QList<QPair<QString, QString>> m_facts;
};

// A section heading between groups of rows (IBM Plex Sans 15, weight 600).
QLabel *sectionHeading(const QString &text, QWidget *parent = nullptr);
// A combo box in the Karyo look, sized for a setting row.
QComboBox *settingCombo(QWidget *parent = nullptr);
// Muted text, wrapped.
QLabel *note(const QString &text, QWidget *parent = nullptr);

// One section of Settings: a scrolling column under a IBM Plex Sans title.
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    SettingsPage(const QString &title, QWidget *parent = nullptr);
    QVBoxLayout *column() const { return m_column; }
    QString title() const { return m_title; }
    // Words "Find a setting" matches against, besides the row titles.
    void addKeywords(const QStringList &words) { m_keywords += words; }
    QStringList keywords() const;
    // Called when the page is shown, to refresh what it reads from the system.
    virtual void refresh() {}

Q_SIGNALS:
    // A row leads to a page of its own (Background, Taskbar, Fonts).
    void subPageRequested(SettingsPage *page);

private:
    QString m_title;
    QVBoxLayout *m_column;
    QStringList m_keywords;
};

// Runs a bounded asynchronous command; successful output or failure is delivered once.
void runCommand(QObject *context, const QString &program, const QStringList &args,
                const std::function<void(const QString &)> &done,
                const std::function<void(const QString &)> &failed = {}, int timeoutMs = 15000);
// Opens a terminal running commandLine; the terminal stays open afterwards.
void runInTerminal(const QString &commandLine);
