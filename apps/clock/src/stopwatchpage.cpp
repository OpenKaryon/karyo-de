// SPDX-License-Identifier: GPL-3.0-or-later
#include "stopwatchpage.h"

#include "clockformat.h"

#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QScrollArea>
#include <QShortcut>
#include <QTimer>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

// Laps, newest first: number, lap time and the total at that lap.
class LapList : public QWidget
{
    Q_OBJECT
public:
    explicit LapList(QWidget *parent = nullptr)
        : QWidget(parent)
    {
    }

    void setLaps(const QList<qint64> &totals)
    {
        m_totals = totals;
        setFixedHeight(int(totals.size()) * RowHeight + (totals.isEmpty() ? 0 : RowHeight));
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        if (m_totals.isEmpty())
            return;
        const Ui::Theme &t = Ui::Theme::current();
        QPainter p(this);
        QList<qint64> laps;
        for (int i = 0; i < m_totals.size(); ++i)
            laps.append(m_totals.at(i) - (i > 0 ? m_totals.at(i - 1) : 0));
        int fastest = -1, slowest = -1;
        if (laps.size() > 1) {
            fastest = int(std::min_element(laps.begin(), laps.end()) - laps.begin());
            slowest = int(std::max_element(laps.begin(), laps.end()) - laps.begin());
        }
        const int w = width();
        auto columns = [w](int y) {
            return std::array<QRect, 3>{QRect(16, y, w / 3 - 16, RowHeight), QRect(w / 3, y, w / 3, RowHeight),
                                        QRect(2 * w / 3, y, w / 3 - 16, RowHeight)};
        };
        p.setFont(Ui::bodyFont(12.5));
        p.setPen(t.muted);
        auto head = columns(0);
        p.drawText(head[0], Qt::AlignVCenter | Qt::AlignLeft, tr("Lap"));
        p.drawText(head[1], Qt::AlignVCenter | Qt::AlignRight, tr("Time"));
        p.drawText(head[2], Qt::AlignVCenter | Qt::AlignRight, tr("Total"));
        p.setPen(t.edge);
        p.drawLine(16, RowHeight - 1, w - 16, RowHeight - 1);
        for (int row = 0; row < laps.size(); ++row) {
            const int i = int(laps.size()) - 1 - row;  // newest first
            auto cells = columns((row + 1) * RowHeight);
            QColor ink = t.text;
            QString mark;
            if (i == fastest) {
                ink = Ui::sage(500);
                mark = tr("fastest");
            } else if (i == slowest) {
                ink = QColor(0xc6, 0x71, 0x39);  // terracotta, whatever the accent
                mark = tr("slowest");
            }
            p.setPen(ink);
            p.setFont(Ui::bodyFont(14));
            p.drawText(cells[0], Qt::AlignVCenter | Qt::AlignLeft,
                       mark.isEmpty() ? QString::number(i + 1) : QStringLiteral("%1  · %2").arg(i + 1).arg(mark));
            p.setFont(ClockFormat::digits(15, QFont::DemiBold));
            p.drawText(cells[1], Qt::AlignVCenter | Qt::AlignRight, ClockFormat::stopwatch(laps.at(i)));
            p.setPen(t.muted);
            p.setFont(ClockFormat::digits(15));
            p.drawText(cells[2], Qt::AlignVCenter | Qt::AlignRight, ClockFormat::stopwatch(m_totals.at(i)));
        }
    }

private:
    static constexpr int RowHeight = 40;
    QList<qint64> m_totals;
};

StopwatchPage::StopwatchPage(QWidget *parent)
    : QWidget(parent)
    , m_time(new QLabel)
    , m_start(new Ui::PillButton(QStringLiteral("play"), tr("Start")))
    , m_lap(new Ui::PillButton(QStringLiteral("flag"), tr("Lap")))
    , m_reset(new Ui::PillButton(QStringLiteral("rotate-ccw"), tr("Reset")))
    , m_laps(new LapList)
    , m_tick(new QTimer(this))
{
    setFocusPolicy(Qt::StrongFocus);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(28, 22, 28, 0);
    v->setSpacing(10);
    auto *title = new QLabel(tr("Stopwatch"));
    title->setFont(Ui::bodyFont(22, QFont::DemiBold));
    v->addWidget(title);
    v->addSpacing(30);
    m_time->setFont(ClockFormat::digits(76, QFont::DemiBold));
    m_time->setAlignment(Qt::AlignCenter);
    v->addWidget(m_time);
    v->addSpacing(14);

    auto *buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    buttons->addStretch(1);
    m_start->setKind(Ui::PillButton::Kind::Primary);
    m_lap->setKind(Ui::PillButton::Kind::Secondary);
    m_reset->setKind(Ui::PillButton::Kind::Secondary);
    for (Ui::PillButton *button : {m_reset, m_start, m_lap}) {
        button->setFixedHeight(44);
        button->setFocusPolicy(Qt::NoFocus);
        buttons->addWidget(button);
    }
    buttons->addStretch(1);
    v->addLayout(buttons);
    connect(m_start, &QAbstractButton::clicked, this, &StopwatchPage::toggle);
    connect(m_lap, &QAbstractButton::clicked, this, &StopwatchPage::lap);
    connect(m_reset, &QAbstractButton::clicked, this, &StopwatchPage::reset);
    v->addSpacing(18);

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget;
    auto *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(0, 0, 0, 16);
    innerLayout->addWidget(m_laps);
    innerLayout->addStretch(1);
    scroll->setWidget(inner);
    scroll->viewport()->setAutoFillBackground(false);
    inner->setAutoFillBackground(false);
    scroll->setMaximumWidth(620);
    v->addWidget(scroll, 1, Qt::AlignHCenter);
    scroll->setMinimumWidth(520);

    auto shortcut = [this](Qt::Key key, void (StopwatchPage::*slot)()) {
        auto *s = new QShortcut(QKeySequence(key), this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, slot);
    };
    shortcut(Qt::Key_Space, &StopwatchPage::toggle);
    shortcut(Qt::Key_L, &StopwatchPage::lap);
    shortcut(Qt::Key_R, &StopwatchPage::reset);

    m_tick->setInterval(31);
    connect(m_tick, &QTimer::timeout, this, &StopwatchPage::refresh);
    refresh();
}

qint64 StopwatchPage::elapsed() const { return m_before + (m_running ? m_clock.elapsed() : 0); }

void StopwatchPage::toggle()
{
    if (m_running) {
        m_before = elapsed();
        m_running = false;
        m_tick->stop();
    } else {
        m_clock.start();
        m_running = true;
        m_tick->start();
    }
    refresh();
}

void StopwatchPage::lap()
{
    if (!m_running)
        return;
    m_lapTotals.append(elapsed());
    m_laps->setLaps(m_lapTotals);
}

void StopwatchPage::reset()
{
    if (m_running)
        return;
    m_before = 0;
    m_lapTotals.clear();
    m_laps->setLaps(m_lapTotals);
    refresh();
}

void StopwatchPage::refresh()
{
    m_time->setText(ClockFormat::stopwatch(elapsed()));
    m_start->setGlyph(m_running ? QStringLiteral("pause") : QStringLiteral("play"));
    m_start->setText(m_running ? tr("Pause") : elapsed() > 0 ? tr("Resume") : tr("Start"));
    m_lap->setEnabled(m_running);
    m_reset->setEnabled(!m_running && elapsed() > 0);
}

void StopwatchPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    setFocus();
    // Hidden, it does not repaint; the time is kept by the clock, not the ticks.
    if (m_running)
        m_tick->start();
    refresh();
}

void StopwatchPage::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    m_tick->stop();
}

#include "stopwatchpage.moc"
