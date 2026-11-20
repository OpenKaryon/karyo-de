// SPDX-License-Identifier: GPL-3.0-or-later
#include "timerspage.h"

#include "clockformat.h"

#include <karyo/ui/menu.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QDateTime>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;
using State = TimerModel::State;

namespace {

constexpr int CardWidth = 246;
constexpr int CardHeight = 282;
constexpr int Gap = 16;
constexpr qint64 Minute = 60 * 1000;

QLabel *mutedLabel(const QString &text, qreal size)
{
    auto *label = new QLabel(text);
    label->setFont(Ui::bodyFont(size));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, Ui::Theme::current().muted);
    label->setPalette(palette);
    return label;
}

} // namespace

TimerCard::TimerCard(TimerModel *model, const QString &id, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_id(id)
    , m_name(new QLabel)
    , m_primary(new Ui::PillButton(QStringLiteral("play"), QString()))
    , m_reset(new Ui::IconButton(QStringLiteral("rotate-ccw"), nullptr, {40, 40}, 18))
    , m_more(new Ui::PillButton(QString(), tr("+1 min")))
{
    setFixedSize(CardWidth, CardHeight);
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(20, 12, 10, 16);
    auto *top = new QHBoxLayout;
    m_name->setFont(Ui::bodyFont(15, QFont::DemiBold));
    top->addWidget(m_name, 1);
    auto *menu = new Ui::IconButton(QStringLiteral("ellipsis"), this, {34, 34}, 18);
    menu->setHint(tr("More"));
    connect(menu, &QAbstractButton::clicked, this, &TimerCard::showMenu);
    top->addWidget(menu);
    v->addLayout(top);
    v->addStretch(1);

    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(0, 0, 10, 0);
    bottom->setSpacing(8);
    bottom->addStretch(1);
    m_reset->setHint(tr("Reset"));
    connect(m_reset, &QAbstractButton::clicked, this, [this] {
        if (m_model->timers().at(index()).state == State::Done)
            Q_EMIT stopRequested(m_id);
        else
            m_model->reset(index());
    });
    bottom->addWidget(m_reset);
    m_primary->setKind(Ui::PillButton::Kind::Primary);
    m_primary->setFixedHeight(44);
    connect(m_primary, &QAbstractButton::clicked, this, [this] {
        const int i = index();
        switch (m_model->timers().at(i).state) {
        case State::Running:
            m_model->pause(i);
            break;
        case State::Done:
            Q_EMIT stopRequested(m_id);
            break;
        default:
            m_model->start(i);
            break;
        }
    });
    bottom->addWidget(m_primary);
    m_more->setKind(Ui::PillButton::Kind::Secondary);
    m_more->setFixedHeight(40);
    connect(m_more, &QAbstractButton::clicked, this, [this] {
        const bool done = m_model->timers().at(index()).state == State::Done;
        if (done)
            Q_EMIT stopRequested(m_id);  // stops the alarm; the minute then runs
        m_model->addTime(index(), Minute);
    });
    bottom->addWidget(m_more);
    bottom->addStretch(1);
    v->addLayout(bottom);
    refresh();
}

void TimerCard::refresh()
{
    const int i = index();
    if (i < 0)
        return;
    const TimerModel::Timer &t = m_model->timers().at(i);
    m_name->setText(QFontMetrics(m_name->font()).elidedText(t.name, Qt::ElideRight, 170));
    m_name->setToolTip(t.name);
    switch (t.state) {
    case State::Running:
        m_primary->setGlyph(QStringLiteral("pause"));
        m_primary->setToolTip(tr("Pause"));
        break;
    case State::Done:
        m_primary->setGlyph(QStringLiteral("square"));
        m_primary->setToolTip(tr("Stop"));
        break;
    default:
        m_primary->setGlyph(QStringLiteral("play"));
        m_primary->setToolTip(t.state == State::Paused ? tr("Resume") : tr("Start"));
        break;
    }
    m_reset->setEnabled(t.state != State::Ready);
    update();
}

void TimerCard::paintEvent(QPaintEvent *)
{
    const Ui::Theme &t = Ui::Theme::current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int i = index();
    if (i < 0)
        return;
    const TimerModel::Timer &timer = m_model->timers().at(i);
    const bool done = timer.state == State::Done;
    Ui::fillRounded(p, QRectF(rect()).adjusted(1, 1, -1, -1), 8, t.card, done ? t.accent : t.edge);
    if (done) {
        p.setPen(QPen(t.accent, 2));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
    }

    // The ring: the time left out of the whole.
    const qint64 left = m_model->remaining(i);
    const QPointF center(width() / 2.0, 132);
    const qreal radius = 70;
    const QRectF ring(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius);
    p.setPen(QPen(Ui::mix(t.text, t.card, 10), 9, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(ring);
    const qreal fraction = done ? 1.0 : qBound(0.0, double(left) / double(qMax(timer.duration, left)), 1.0);
    if (fraction > 0) {
        p.setPen(QPen(timer.state == State::Paused ? t.muted : t.accent, 9, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(ring, 90 * 16, int(fraction * 360 * 16));
    }

    p.setPen(t.text);
    p.setFont(ClockFormat::digits(left >= 3600 * 1000 ? 26 : 32, QFont::DemiBold));
    p.drawText(QRectF(center.x() - radius, center.y() - 26, 2 * radius, 40), Qt::AlignCenter,
               ClockFormat::timer(left));
    QString note;
    QColor noteColor = t.muted;
    const QString clock = QLocale().toString(QDateTime::fromMSecsSinceEpoch(done ? timer.finishedAt : timer.deadline).time(),
                                             QLocale::ShortFormat);
    switch (timer.state) {
    case State::Running:
        note = tr("Ends %1").arg(clock);
        break;
    case State::Paused:
        note = tr("Paused");
        break;
    case State::Done:
        note = tr("Done at %1").arg(clock);
        noteColor = t.accent;
        break;
    case State::Ready:
        note = tr("Ready");
        break;
    }
    p.setPen(noteColor);
    p.setFont(Ui::bodyFont(12.5, done ? QFont::DemiBold : QFont::Normal));
    p.drawText(QRectF(center.x() - radius, center.y() + 16, 2 * radius, 20), Qt::AlignCenter, note);
}

void TimerCard::mouseReleaseEvent(QMouseEvent *event)
{
    // A finished timer: a click anywhere silences it.
    if (event->button() == Qt::LeftButton && index() >= 0 && m_model->timers().at(index()).state == State::Done)
        Q_EMIT stopRequested(m_id);
}

void TimerCard::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
        Q_EMIT editRequested(m_id);
}

void TimerCard::showMenu()
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    menu->addAction(tr("Edit…"), this, [this] { Q_EMIT editRequested(m_id); });
    menu->addAction(tr("Delete"), this, [this] {
        if (m_model->timers().at(index()).state == State::Done)
            Q_EMIT stopRequested(m_id);
        m_model->remove(index());
    });
    menu->popup(QCursor::pos());
}

TimerDialog::TimerDialog(const QString &name, qint64 duration, QWidget *parent)
    : QDialog(parent)
    , m_name(new QLineEdit)
    , m_hours(new QSpinBox)
    , m_minutes(new QSpinBox)
    , m_seconds(new QSpinBox)
{
    const bool adding = duration == 0;
    setWindowTitle(adding ? tr("New timer") : tr("Edit timer"));
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(24, 20, 24, 20);
    v->setSpacing(8);
    auto *heading = new QLabel(windowTitle());
    heading->setFont(Ui::bodyFont(19, QFont::DemiBold));
    v->addWidget(heading);
    v->addSpacing(6);
    v->addWidget(mutedLabel(tr("Name"), 13));
    m_name->setText(name);
    m_name->setPlaceholderText(tr("Timer"));
    m_name->setMinimumHeight(40);
    m_name->setFont(Ui::bodyFont(15));
    v->addWidget(m_name);
    v->addSpacing(4);
    v->addWidget(mutedLabel(tr("Duration"), 13));
    auto *row = new QHBoxLayout;
    row->setSpacing(6);
    const QList<QPair<QSpinBox *, QString>> parts = {{m_hours, tr("h")}, {m_minutes, tr("min")}, {m_seconds, tr("s")}};
    for (const auto &part : parts) {
        part.first->setRange(0, part.first == m_hours ? 99 : 59);
        part.first->setMinimumSize(72, 40);
        part.first->setFont(ClockFormat::digits(16));
        part.first->setAlignment(Qt::AlignCenter);
        row->addWidget(part.first);
        row->addWidget(mutedLabel(part.second, 13));
        row->addSpacing(8);
    }
    row->addStretch(1);
    v->addLayout(row);
    setDuration(adding ? 5 * Minute : duration);

    if (adding) {
        // The bench's usual timers.
        v->addSpacing(8);
        v->addWidget(mutedLabel(tr("Quick picks"), 13));
        auto *grid = new QGridLayout;
        grid->setSpacing(6);
        const QList<QPair<QString, int>> presets = {
            {tr("Spin down"), 1},  {tr("Wash"), 5},     {tr("Centrifuge"), 10}, {tr("Thaw"), 15},
            {tr("Staining"), 30},  {tr("Gel run"), 45}, {tr("Incubation"), 60}, {tr("Overnight"), 16 * 60},
        };
        for (int i = 0; i < presets.size(); ++i) {
            const QString label = presets.at(i).first;
            const int minutes = presets.at(i).second;
            const QString length = minutes % 60 == 0 ? tr("%1 h").arg(minutes / 60) : tr("%1 min").arg(minutes);
            auto *chip = new Ui::PillButton(QString(), label + QStringLiteral(" · ") + length, this);
            chip->setKind(Ui::PillButton::Kind::Secondary);
            connect(chip, &QAbstractButton::clicked, this, [this, label, minutes] {
                m_name->setText(label);
                setDuration(minutes * Minute);
            });
            grid->addWidget(chip, i / 2, i % 2);
        }
        v->addLayout(grid);
    }

    v->addSpacing(14);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch(1);
    auto *cancel = new Ui::PillButton(QString(), tr("Cancel"), this);
    cancel->setKind(Ui::PillButton::Kind::Secondary);
    connect(cancel, &QAbstractButton::clicked, this, &QDialog::reject);
    auto *ok = new Ui::PillButton(QString(), adding ? tr("Add") : tr("Save"), this);
    ok->setKind(Ui::PillButton::Kind::Primary);
    connect(ok, &QAbstractButton::clicked, this, [this] {
        if (this->duration() > 0)
            accept();
    });
    buttons->addWidget(cancel);
    buttons->addWidget(ok);
    v->addLayout(buttons);
    m_name->setFocus();
    m_name->selectAll();
}

void TimerDialog::setDuration(qint64 ms)
{
    const qint64 seconds = ms / 1000;
    m_hours->setValue(int(seconds / 3600));
    m_minutes->setValue(int(seconds / 60 % 60));
    m_seconds->setValue(int(seconds % 60));
}

QString TimerDialog::name() const
{
    const QString name = m_name->text().trimmed();
    return name.isEmpty() ? tr("Timer") : name;
}

qint64 TimerDialog::duration() const
{
    return (qint64(m_hours->value()) * 3600 + m_minutes->value() * 60 + m_seconds->value()) * 1000;
}

TimersPage::TimersPage(TimerModel *model, QWidget *parent)
    : QWidget(parent)
    , m_model(model)
    , m_grid(new QWidget)
    , m_empty(mutedLabel(tr("No timers yet. Add one for the centrifuge, a gel or an incubation; several can run "
                            "at once."),
                         14))
{
    auto *v = new QVBoxLayout(this);
    v->setContentsMargins(28, 22, 20, 0);
    v->setSpacing(14);
    auto *header = new QHBoxLayout;
    auto *title = new QLabel(tr("Timers"));
    title->setFont(Ui::bodyFont(22, QFont::DemiBold));
    header->addWidget(title);
    header->addStretch(1);
    auto *add = new Ui::PillButton(QStringLiteral("plus"), tr("Add timer"), this);
    add->setKind(Ui::PillButton::Kind::Primary);
    connect(add, &QAbstractButton::clicked, this, &TimersPage::addTimer);
    header->addWidget(add);
    header->addSpacing(8);
    v->addLayout(header);
    m_empty->setWordWrap(true);
    v->addWidget(m_empty);

    auto *scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *inner = new QWidget;
    auto *innerLayout = new QVBoxLayout(inner);
    innerLayout->setContentsMargins(0, 0, 0, 20);
    auto *grid = new QGridLayout(m_grid);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(Gap);
    innerLayout->addWidget(m_grid);
    innerLayout->addStretch(1);
    scroll->setWidget(inner);
    scroll->viewport()->setAutoFillBackground(false);
    inner->setAutoFillBackground(false);
    v->addWidget(scroll, 1);

    connect(model, &TimerModel::changed, this, &TimersPage::rebuild);
    rebuild();
}

void TimersPage::rebuild()
{
    QStringList ids;
    for (const TimerModel::Timer &t : m_model->timers())
        ids.append(t.id);
    if (ids == m_order) {
        refresh();
        return;
    }
    for (auto it = m_cards.begin(); it != m_cards.end();) {
        if (!ids.contains(it.key())) {
            it.value()->deleteLater();
            it = m_cards.erase(it);
        } else {
            ++it;
        }
    }
    for (const QString &id : std::as_const(ids)) {
        if (m_cards.contains(id))
            continue;
        auto *card = new TimerCard(m_model, id, m_grid);
        connect(card, &TimerCard::stopRequested, this, &TimersPage::stopRequested);
        connect(card, &TimerCard::editRequested, this, &TimersPage::editTimer);
        m_cards.insert(id, card);
    }
    m_order = ids;
    m_columns = 0;
    relayout();
    refresh();
}

void TimersPage::relayout()
{
    const int columns = qMax(1, (width() - 48 + Gap) / (CardWidth + Gap));
    if (columns == m_columns)
        return;
    m_columns = columns;
    auto *grid = static_cast<QGridLayout *>(m_grid->layout());
    while (grid->count() > 0)
        delete grid->takeAt(0);
    for (int i = 0; i < m_order.size(); ++i)
        grid->addWidget(m_cards.value(m_order.at(i)), i / columns, i % columns);
    // setColumnStretch adds columns, so the count is taken first.
    const int last = qMax(columns, grid->columnCount() - 1);
    for (int c = 0; c <= last; ++c)
        grid->setColumnStretch(c, c == columns ? 1 : 0);
    m_empty->setVisible(m_order.isEmpty());
}

void TimersPage::refresh()
{
    for (TimerCard *card : std::as_const(m_cards))
        card->refresh();
}

void TimersPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    relayout();
}

void TimersPage::addTimer()
{
    TimerDialog dialog(QString(), 0, this);
    if (dialog.exec() == QDialog::Accepted)
        m_model->add(dialog.name(), dialog.duration());
}

void TimersPage::editTimer(const QString &id)
{
    const int index = m_model->indexOf(id);
    if (index < 0)
        return;
    const TimerModel::Timer t = m_model->timers().at(index);
    TimerDialog dialog(t.name, t.duration, this);
    if (dialog.exec() == QDialog::Accepted && m_model->indexOf(id) >= 0)
        m_model->edit(m_model->indexOf(id), dialog.name(), dialog.duration());
}
