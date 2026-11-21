// SPDX-License-Identifier: GPL-3.0-or-later
#include "widgets.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QComboBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QTimer>
#include <QPointer>
#include <QCoreApplication>
#include <memory>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

void setInk(QLabel *label, const QColor &color)
{
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, color);
    label->setPalette(palette);
}

} // namespace

// ---------------------------------------------------------------- ToggleSwitch

ToggleSwitch::ToggleSwitch(QWidget *parent)
    : QAbstractButton(parent)
{
    setCheckable(true);
    setAttribute(Qt::WA_Hover);
    setFocusPolicy(Qt::TabFocus);
    setFixedSize(46, 26);
    setCursor(Qt::PointingHandCursor);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
    if (isChecked()) {
        Ui::fillPill(painter, track, underMouse() ? Ui::accent(600) : t.accent);
        Ui::fillPill(painter, QRectF(width() - 4 - 17 - 1, (height() - 17) / 2.0, 17, 17), Ui::neutral(100));
    } else {
        painter.setPen(QPen(t.muted, 1.5));
        painter.setBrush(underMouse() ? t.hover : Qt::transparent);
        painter.drawRoundedRect(track, track.height() / 2, track.height() / 2);
        Ui::fillPill(painter, QRectF(5, (height() - 15) / 2.0, 15, 15), t.muted);
    }
    if (hasFocus()) {
        painter.setPen(QPen(t.accent, 2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0, 0, 0, 0), height() / 2.0, height() / 2.0);
    }
}

// ---------------------------------------------------------------- Segmented

Segmented::Segmented(const QStringList &options, QWidget *parent)
    : QWidget(parent)
    , m_options(options)
{
    setMouseTracking(true);
    setFixedSize(sizeHint());
    setCursor(Qt::PointingHandCursor);
}

QSize Segmented::sizeHint() const
{
    const QFontMetrics metrics(Ui::bodyFont(13, QFont::DemiBold));
    int w = 6;
    for (const QString &option : m_options)
        w += metrics.horizontalAdvance(option) + 24;
    return {w, 38};
}

QRect Segmented::optionRect(int index) const
{
    const QFontMetrics metrics(Ui::bodyFont(13, QFont::DemiBold));
    int x = 3;
    for (int i = 0; i < index; ++i)
        x += metrics.horizontalAdvance(m_options.at(i)) + 24;
    return QRect(x, 3, metrics.horizontalAdvance(m_options.at(index)) + 24, height() - 6);
}

void Segmented::setCurrentIndex(int index)
{
    if (index == m_current || index < 0 || index >= m_options.size())
        return;
    m_current = index;
    update();
    Q_EMIT currentChanged(index);
}

void Segmented::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF outline = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    Ui::fillRounded(painter, outline, outline.height() / 2, Ui::alpha(Ui::neutral(100), 50), t.edge);
    painter.setFont(Ui::bodyFont(13, QFont::DemiBold));
    for (int i = 0; i < m_options.size(); ++i) {
        const QRect r = optionRect(i);
        if (i == m_current)
            Ui::fillPill(painter, r, t.accent);
        else if (i == m_hover)
            Ui::fillPill(painter, r, t.hover);
        painter.setPen(i == m_current ? Ui::neutral(100) : t.text);
        painter.drawText(r, Qt::AlignCenter, m_options.at(i));
    }
}

void Segmented::mouseReleaseEvent(QMouseEvent *event)
{
    for (int i = 0; i < m_options.size(); ++i) {
        if (optionRect(i).contains(event->position().toPoint()))
            setCurrentIndex(i);
    }
}

void Segmented::mouseMoveEvent(QMouseEvent *event)
{
    int hover = -1;
    for (int i = 0; i < m_options.size(); ++i) {
        if (optionRect(i).contains(event->position().toPoint()))
            hover = i;
    }
    if (hover != m_hover) {
        m_hover = hover;
        update();
    }
}

void Segmented::leaveEvent(QEvent *)
{
    m_hover = -1;
    update();
}

// ---------------------------------------------------------------- SettingRow

SettingRow::SettingRow(const QString &glyph, const QString &title, const QString &description, QWidget *parent)
    : QWidget(parent)
    , m_glyph(glyph)
    , m_description(new QLabel(description, this))
    , m_right(new QHBoxLayout)
{
    setProperty("settingTitle", title);
    setMinimumHeight(68);
    setAttribute(Qt::WA_Hover);
    const Ui::Theme &t = Ui::Theme::current();

    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(22 + 40 + 18, 12, 22, 12);
    row->setSpacing(16);
    auto *text = new QVBoxLayout;
    text->setSpacing(2);
    auto *titleLabel = new QLabel(title, this);
    titleLabel->setTextFormat(Qt::PlainText);
    m_description->setTextFormat(Qt::PlainText);
    titleLabel->setFont(Ui::bodyFont(14, QFont::DemiBold));
    setInk(titleLabel, t.text);
    text->addWidget(titleLabel);
    m_description->setFont(Ui::bodyFont(12.5));
    m_description->setWordWrap(true);
    setInk(m_description, t.muted);
    m_description->setVisible(!description.isEmpty());
    text->addWidget(m_description);
    row->addLayout(text, 1);
    m_right->setSpacing(12);
    row->addLayout(m_right);
}

void SettingRow::setControl(QWidget *control)
{
    control->setParent(this);
    m_right->addWidget(control, 0, Qt::AlignVCenter);
}

void SettingRow::setValue(const QString &value)
{
    if (!m_value) {
        m_value = new QLabel(this);
        m_value->setTextFormat(Qt::PlainText);
        m_value->setFont(Ui::bodyFont(13));
        setInk(m_value, Ui::Theme::current().muted);
        m_right->insertWidget(0, m_value, 0, Qt::AlignVCenter);
    }
    m_value->setText(value);
}

void SettingRow::setDescription(const QString &description)
{
    m_description->setText(description);
    m_description->setVisible(!description.isEmpty());
}

void SettingRow::setLink(const QString &value)
{
    setValue(value);
    if (!m_link) {
        m_link = true;
        auto *chevron = new QLabel(this);
        chevron->setPixmap(Ui::icon(QStringLiteral("chevron-right"), 16, Ui::Theme::current().text,
                                    devicePixelRatioF()));
        m_right->addWidget(chevron, 0, Qt::AlignVCenter);
        setCursor(Qt::PointingHandCursor);
    }
}

ToggleSwitch *SettingRow::addToggle(bool on)
{
    auto *toggle = new ToggleSwitch(this);
    toggle->setChecked(on);
    setValue(on ? tr("On") : tr("Off"));
    connect(toggle, &QAbstractButton::toggled, this, [this](bool checked) { setValue(checked ? tr("On") : tr("Off")); });
    m_right->addWidget(toggle, 0, Qt::AlignVCenter);
    return toggle;
}

void SettingRow::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Ui::fillRounded(painter, rect(), 8, m_link && m_hover ? Ui::mix(t.text, t.card, 5) : t.card);
    const QRectF circle(22, (height() - 40) / 2.0, 40, 40);
    Ui::fillPill(painter, circle, t.scheme == Ui::ColorScheme::Light ? Ui::neutral(100) : t.win);
    painter.drawPixmap(QPointF(circle.x() + 11, circle.y() + 11),
                       Ui::icon(m_glyph, 18, t.text, devicePixelRatioF()));
}

void SettingRow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_link && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        Q_EMIT clicked();
}

void SettingRow::enterEvent(QEnterEvent *)
{
    m_hover = true;
    update();
}

void SettingRow::leaveEvent(QEvent *)
{
    m_hover = false;
    update();
}

// ---------------------------------------------------------------- FactsCard

FactsCard::FactsCard(QWidget *parent)
    : QWidget(parent)
{
}

void FactsCard::setFacts(const QList<QPair<QString, QString>> &facts)
{
    m_facts = facts;
    setFixedHeight(16 + int(m_facts.size()) * 34 + 16);
    update();
}

void FactsCard::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    Ui::fillRounded(painter, rect(), 8, t.card);
    const int labelWidth = qMin(220, width() / 3);
    int y = 16;
    for (const auto &fact : std::as_const(m_facts)) {
        painter.setFont(Ui::bodyFont(13));
        painter.setPen(t.muted);
        painter.drawText(QRect(24, y, labelWidth, 34), Qt::AlignVCenter | Qt::AlignLeft, fact.first);
        painter.setFont(Ui::bodyFont(13.5, QFont::DemiBold));
        painter.setPen(t.text);
        const int x = 24 + labelWidth + 12;
        painter.drawText(QRect(x, y, width() - x - 24, 34), Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(painter.font()).elidedText(fact.second, Qt::ElideMiddle, width() - x - 24));
        y += 34;
    }
}

// ---------------------------------------------------------------- helpers

QLabel *sectionHeading(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setFont(Ui::bodyFont(15, QFont::DemiBold));
    setInk(label, Ui::Theme::current().text);
    label->setContentsMargins(6, 14, 0, 2);
    return label;
}

QComboBox *settingCombo(QWidget *parent)
{
    auto *combo = new QComboBox(parent);
    combo->setMinimumWidth(220);
    combo->setFont(Ui::bodyFont(13));
    return combo;
}

QLabel *note(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setFont(Ui::bodyFont(12.5));
    setInk(label, Ui::Theme::current().muted);
    label->setContentsMargins(6, 0, 6, 0);
    return label;
}

SettingsPage::SettingsPage(const QString &title, QWidget *parent)
    : QWidget(parent)
    , m_title(title)
    , m_column(new QVBoxLayout)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 24);
    outer->setSpacing(22);
    auto *heading = new QLabel(title, this);
    heading->setFont(Ui::headingFont(34));
    setInk(heading, Ui::Theme::current().text);
    outer->addWidget(heading);
    m_column->setSpacing(10);
    outer->addLayout(m_column);
    outer->addStretch(1);
}

QStringList SettingsPage::keywords() const
{
    QStringList words = m_keywords;
    words.append(m_title);
    const auto rows = findChildren<SettingRow *>();
    for (const SettingRow *row : rows)
        words.append(row->property("settingTitle").toString());
    return words;
}

void runCommand(QObject *context, const QString &program, const QStringList &args,
                const std::function<void(const QString &)> &done,
                const std::function<void(const QString &)> &failed, int timeoutMs)
{
    struct State {
        QByteArray output, errors;
        bool completed = false, timedOut = false, overflow = false;
    };
    const auto state = std::make_shared<State>();
    const QPointer<QObject> owner(context ? context : QCoreApplication::instance());
    // Reap asynchronously after page destruction rather than running callbacks
    // from QProcess destruction while the page is already being torn down.
    auto *process = new QProcess(QCoreApplication::instance());
    QObject::connect(owner, &QObject::destroyed, process, [process] { process->kill(); });
    auto *deadline = new QTimer(process);
    deadline->setSingleShot(true);
    const auto finish = [process, deadline, state, done, failed, owner](bool success, QString message) {
        if (state->completed)
            return;
        state->completed = true;
        deadline->stop();
        if (state->timedOut)
            message = QCoreApplication::translate("SettingsPage", "The command timed out.");
        else if (state->overflow)
            message = QCoreApplication::translate("SettingsPage", "The command returned too much data.");
        else if (!success && message.isEmpty())
            message = QString::fromLocal8Bit(state->errors).trimmed();
        if (!success && message.isEmpty())
            message = QCoreApplication::translate("SettingsPage", "The command failed.");
        process->deleteLater(); // The callback may delete its owning page.
        if (!owner)
            return;
        if (success && done)
            done(QString::fromUtf8(state->output));
        else if (!success && failed)
            failed(message);
    };
    const auto drain = [process, state] {
        const QByteArray output = process->readAllStandardOutput();
        const QByteArray errors = process->readAllStandardError();
        constexpr qsizetype OutputLimit = 16 * 1024 * 1024, ErrorLimit = 256 * 1024;
        if (state->output.size() + output.size() > OutputLimit || state->errors.size() + errors.size() > ErrorLimit) {
            state->overflow = true;
            process->kill();
        } else {
            state->output += output;
            state->errors += errors;
        }
    };
    QObject::connect(process, &QProcess::readyReadStandardOutput, process, drain);
    QObject::connect(process, &QProcess::readyReadStandardError, process, drain);
    QObject::connect(process, &QProcess::finished, process, [state, drain, finish](int code, QProcess::ExitStatus status) {
        drain();
        finish(code == 0 && status == QProcess::NormalExit && !state->timedOut && !state->overflow, {});
    });
    QObject::connect(process, &QProcess::errorOccurred, process, [process, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(false, process->errorString());
    });
    QObject::connect(deadline, &QTimer::timeout, process, [process, state] {
        state->timedOut = true;
        process->kill();
    });
    QTimer::singleShot(0, process, [process, deadline, state, program, args, timeoutMs, owner] {
        if (!owner) {
            process->deleteLater();
            return;
        }
        process->start(program, args);
        if (!state->completed)
            deadline->start(qMax(1, timeoutMs));
    });
}

void runInTerminal(const QString &commandLine)
{
    const QString own = QStandardPaths::findExecutable(QStringLiteral("karyo-terminal"));
    QProcess::startDetached(own.isEmpty() ? QStringLiteral("x-terminal-emulator") : own,
                            {QStringLiteral("-e"), QStringLiteral("sh"), QStringLiteral("-c"),
                             commandLine + QStringLiteral("; exec \"${SHELL:-/bin/bash}\"")},
                            QDir::homePath());
}
