// SPDX-License-Identifier: GPL-3.0-or-later
#include "processespage.h"
#include "processactions.h"
#include "filemanager.h"

#include "processmodel.h"
#include "sampler.h"

#include <karyo/ui/icons.h>
#include <karyo/ui/menu.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QProcess>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTreeView>
#include <QVBoxLayout>

#include <csignal>
#include <sys/resource.h>

namespace Ui = Karyo::Ui;

// Column headers with the machine totals above the numeric columns: "64%" over "CPU".
class TotalsHeader : public QHeaderView
{
public:
    explicit TotalsHeader(QWidget *parent)
        : QHeaderView(Qt::Horizontal, parent)
    {
        setFixedHeight(56);
        setSectionsClickable(true);
        setHighlightSections(false);
        setDefaultAlignment(Qt::AlignLeft | Qt::AlignBottom);
    }
    void setTotals(const QList<double> &totals)
    {
        m_totals = totals;
        viewport()->update();
    }

protected:
    void paintSection(QPainter *painter, const QRect &rect, int section) const override
    {
        const Ui::Theme &t = Ui::Theme::current();
        const QString label = model()->headerData(section, Qt::Horizontal).toString();
        const bool numeric = section >= ProcessModel::Cpu;
        const QRect inner = rect.adjusted(12, 6, -12, -8);
        painter->save();
        if (numeric) {
            const int i = section - ProcessModel::Cpu;
            const QString value = i < m_totals.size() ? QStringLiteral("%1%").arg(qRound(m_totals.at(i))) : QString();
            painter->setFont(Ui::bodyFont(18, QFont::DemiBold));
            painter->setPen(t.text);
            painter->drawText(inner.adjusted(0, 0, 0, -16), Qt::AlignRight | Qt::AlignBottom, value);
            painter->setFont(Ui::bodyFont(12));
            painter->setPen(t.muted);
            painter->drawText(inner, Qt::AlignRight | Qt::AlignBottom, label);
        } else {
            painter->setFont(Ui::bodyFont(12, QFont::DemiBold));
            painter->setPen(t.muted);
            painter->drawText(inner, Qt::AlignLeft | Qt::AlignBottom, label);
        }
        if (sortIndicatorSection() == section && isSortIndicatorShown()) {
            const QPixmap arrow = Ui::icon(sortIndicatorOrder() == Qt::AscendingOrder ? QStringLiteral("chevron-up")
                                                                                     : QStringLiteral("chevron-down"),
                                           12, t.muted, painter->device()->devicePixelRatioF());
            const int x = numeric ? inner.right() - painter->fontMetrics().horizontalAdvance(label) - 16
                                  : inner.left() + painter->fontMetrics().horizontalAdvance(label) + 4;
            painter->drawPixmap(x, inner.bottom() - 13, arrow);
        }
        painter->restore();
    }

private:
    QList<double> m_totals;
};

namespace {

class ProcessDelegate : public QStyledItemDelegate
{
public:
    ProcessDelegate(Sampler *sampler, QObject *parent)
        : QStyledItemDelegate(parent)
        , m_sampler(sampler)
    {
    }

    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QSize s = QStyledItemDelegate::sizeHint(option, index);
        s.setHeight(index.data(ProcessModel::GroupRole).toBool() ? 44 : 36);
        return s;
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const Ui::Theme &t = Ui::Theme::current();
        const QRect r = option.rect;
        const int column = index.column();
        if (index.data(ProcessModel::GroupRole).toBool()) {
            painter->save();
            painter->setFont(Ui::bodyFont(13.5, QFont::DemiBold));
            painter->setPen(t.ink1);
            painter->drawText(r.adjusted(4, 6, 0, 0), Qt::AlignLeft | Qt::AlignVCenter, index.data().toString());
            painter->restore();
            return;
        }
        // Heat tint: accent for CPU and disk, sage for memory, scaled by value.
        if (column == ProcessModel::Cpu || column == ProcessModel::Memory || column == ProcessModel::Disk
            || column == ProcessModel::Network) {
            const double value = index.data(ProcessModel::ValueRole).toDouble();
            double level = 0;
            QColor base = t.accent;
            if (column == ProcessModel::Cpu)
                level = value / 50.0;
            else if (column == ProcessModel::Memory) {
                level = m_sampler->memTotal() ? value / (m_sampler->memTotal() * 0.25) : 0;
                base = t.accent2;
            } else if (column == ProcessModel::Disk)
                level = value / (50.0 * 1024 * 1024);
            level = qBound(0.0, level, 1.0);
            const qreal pct = column == ProcessModel::Memory ? 4 + 32 * level : 4 + 30 * level;
            painter->fillRect(r, Ui::alpha(base, pct));
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        // The view paints the rounded selection; cells only draw content.
        QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
        style->drawPrimitive(QStyle::PE_PanelItemViewItem, &opt, painter, opt.widget);
        painter->save();
        const bool selected = option.state & QStyle::State_Selected;
        const QColor ink = selected ? t.selInk : t.text;
        if (column == ProcessModel::Name) {
            int x = r.left() + 4;
            const QVariantMap style = index.data(ProcessModel::StyleRole).toMap();
            if (!style.isEmpty()) {
                const QRectF tile(x, r.center().y() - 11, 22, 22);
                const QColor tileColor = style.value(QStringLiteral("tile")).value<QColor>();
                const QColor glyphInk = style.value(QStringLiteral("ink")).value<QColor>();
                const QString glyph = style.value(QStringLiteral("glyph")).toString();
                const QString mono = style.value(QStringLiteral("monogram")).toString();
                if (tileColor.isValid())
                    Ui::fillRounded(*painter, tile, 7, tileColor);
                if (!glyph.isEmpty()) {
                    painter->drawPixmap(QPointF(tile.left() + 4, tile.top() + 4),
                                        Ui::icon(glyph, 14, glyphInk, painter->device()->devicePixelRatioF()));
                } else if (!mono.isEmpty()) {
                    painter->setFont(Ui::bodyFont(11, QFont::Bold));
                    painter->setPen(glyphInk);
                    painter->drawText(tile, Qt::AlignCenter, mono);
                } else {
                    style.value(QStringLiteral("icon")).value<QIcon>().paint(painter, tile.toRect());
                }
                x += 22 + 10;
            }
            painter->setFont(Ui::bodyFont(13.5, selected ? QFont::DemiBold : QFont::Normal));
            painter->setPen(ink);
            const QRect text(x, r.top(), r.right() - x - 6, r.height());
            painter->drawText(text, Qt::AlignVCenter | Qt::AlignLeft,
                              painter->fontMetrics().elidedText(index.data().toString(), Qt::ElideRight, text.width()));
        } else if (column == ProcessModel::Environment) {
            const QString env = index.data().toString();
            if (!env.isEmpty()) {
                painter->setFont(Ui::bodyFont(11.5, QFont::DemiBold));
                const int w = painter->fontMetrics().horizontalAdvance(env) + 18;
                const QRectF tag(r.left() + 4, r.center().y() - 11, w, 22);
                Ui::fillRounded(*painter, tag, 11, t.tint2);
                painter->setPen(t.ink2);
                painter->drawText(tag, Qt::AlignCenter, env);
            }
        } else {
            painter->setFont(Ui::bodyFont(13));
            painter->setPen(column == ProcessModel::Network ? t.muted : ink);
            const QString text = column == ProcessModel::Network ? QStringLiteral("—") : index.data().toString();
            painter->drawText(r.adjusted(8, 0, -12, 0), Qt::AlignVCenter | Qt::AlignRight, text);
        }
        painter->restore();
    }

private:
    Sampler *m_sampler;
};

} // namespace

ProcessesPage::ProcessesPage(Sampler *sampler, QWidget *parent)
    : QWidget(parent)
    , m_sampler(sampler)
    , m_model(new ProcessModel(sampler, this))
    , m_view(new QTreeView)
    , m_header(new TotalsHeader(m_view))
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 16, 12);
    layout->setSpacing(10);

    auto *top = new QHBoxLayout;
    auto *title = new QLabel(tr("Processes"));
    title->setFont(Ui::headingFont(26));
    top->addWidget(title);
    top->addStretch();
    auto *run = new Ui::PillButton(QStringLiteral("circle-plus"), tr("Run new task"));
    auto *end = new Ui::PillButton(QStringLiteral("circle-x"), tr("End task"));
    end->setKind(Ui::PillButton::Kind::Tint);
    auto *efficiency = new Ui::PillButton(QStringLiteral("leaf"), tr("Efficiency mode"));
    auto *view = new Ui::PillButton(QString(), tr("View"));
    view->setChevron(true);
    for (auto *button : {run, end, efficiency, view})
        top->addWidget(button);
    layout->addLayout(top);
    connect(run, &QAbstractButton::clicked, this, &ProcessesPage::runNewTask);
    connect(end, &QAbstractButton::clicked, this, &ProcessesPage::endTask);
    connect(efficiency, &QAbstractButton::clicked, this, &ProcessesPage::efficiencyMode);
    connect(view, &QAbstractButton::clicked, this, &ProcessesPage::showViewMenu);

    m_view->setHeader(m_header);
    m_view->setModel(m_model);
    m_view->setItemDelegate(new ProcessDelegate(sampler, m_view));
    m_view->setFrameShape(QFrame::NoFrame);
    m_view->setUniformRowHeights(false);
    m_view->setIndentation(20);
    // Groups are root items: no expander, as in the design.
    m_view->setRootIsDecorated(false);
    m_view->setItemsExpandable(true);
    m_view->setSortingEnabled(true);
    m_view->sortByColumn(ProcessModel::Name, Qt::AscendingOrder);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setAttribute(Qt::WA_TranslucentBackground);
    QPalette palette = m_view->palette();
    palette.setColor(QPalette::Base, Qt::transparent);
    m_view->setPalette(palette);
    m_header->setStretchLastSection(false);
    m_header->viewport()->setAutoFillBackground(false);
    QPalette headerPalette = m_header->palette();
    headerPalette.setColor(QPalette::Button, Qt::transparent);
    headerPalette.setColor(QPalette::Window, Qt::transparent);
    m_header->setPalette(headerPalette);
    m_header->setSectionResizeMode(ProcessModel::Name, QHeaderView::Stretch);
    m_header->resizeSection(ProcessModel::Environment, 130);
    for (int c = ProcessModel::Cpu; c < ProcessModel::ColumnCount; ++c)
        m_header->resizeSection(c, 110);
    layout->addWidget(m_view, 1);

    connect(m_view, &QWidget::customContextMenuRequested, this, &ProcessesPage::showContextMenu);
    connect(m_model, &ProcessModel::aboutToRebuild, this, &ProcessesPage::saveState);
    connect(m_model, &ProcessModel::rebuilt, this, &ProcessesPage::restoreState);
    connect(m_view, &QTreeView::expanded, this, [this](const QModelIndex &index) {
        m_expanded.insert(index.data(ProcessModel::KeyRole).toString());
    });
    connect(m_view, &QTreeView::collapsed, this, [this](const QModelIndex &index) {
        m_expanded.remove(index.data(ProcessModel::KeyRole).toString());
    });
    m_expanded = {QStringLiteral("group:apps"), QStringLiteral("group:background")};
    restoreState();
}

void ProcessesPage::setFilter(const QString &text) { m_model->setFilter(text); }

void ProcessesPage::saveState()
{
    const QModelIndex current = m_view->currentIndex();
    m_selectedKey = current.isValid() ? current.data(ProcessModel::KeyRole).toString() : QString();
    m_scroll = m_view->verticalScrollBar()->value();
}

void ProcessesPage::restoreState()
{
    for (int row = 0; row < m_model->rowCount(); ++row) {
        const QModelIndex group = m_model->index(row, 0);
        m_view->setFirstColumnSpanned(row, QModelIndex(), true);
        if (m_expanded.contains(group.data(ProcessModel::KeyRole).toString()))
            m_view->expand(group);
        for (int child = 0; child < m_model->rowCount(group); ++child) {
            const QModelIndex index = m_model->index(child, 0, group);
            if (m_expanded.contains(index.data(ProcessModel::KeyRole).toString()))
                m_view->expand(index);
        }
    }
    if (!m_selectedKey.isEmpty()) {
        const QModelIndex index = m_model->indexForKey(m_selectedKey);
        if (index.isValid())
            m_view->selectionModel()->setCurrentIndex(
                index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    }
    m_view->verticalScrollBar()->setValue(m_scroll);
    m_header->setTotals({m_sampler->cpuPercent(), m_sampler->memoryPercent(), m_sampler->diskPercent(),
                         m_sampler->networkPercent()});
}

QList<ProcessInfo> ProcessesPage::selectedProcesses() const
{
    QList<ProcessInfo> processes;
    QSet<int> seen;
    for (const auto &index : m_view->selectionModel()->selectedRows()) {
        for (const QVariant &pid : index.data(ProcessModel::PidsRole).toList()) {
            const int value = pid.toInt();
            if (!seen.contains(value) && m_sampler->processes().contains(value)) {
                processes.append(m_sampler->processes().value(value));
                seen.insert(value);
            }
        }
    }
    return processes;
}

void ProcessesPage::applyAction(const QList<ProcessInfo> &processes, int signal)
{
    QStringList errors;
    for (const auto &process : processes) {
        QString error;
        const bool ok = signal ? ProcessActions::signal(process, signal, &error)
                               : ProcessActions::efficiency(process, &error);
        if (!ok)
            errors.append(QStringLiteral("%1 (%2): %3").arg(process.name).arg(process.pid).arg(error));
    }
    m_sampler->refresh();
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("System Monitor"), errors.join(QLatin1Char('\n')));
}

void ProcessesPage::endTask() { applyAction(selectedProcesses(), SIGTERM); }
void ProcessesPage::efficiencyMode() { applyAction(selectedProcesses(), 0); }

void ProcessesPage::runNewTask()
{
    bool ok = false;
    const QString command = QInputDialog::getText(this, tr("Run new task"),
                                                  tr("Type the name of a program or a command:"),
                                                  QLineEdit::Normal, QString(), &ok);
    if (ok && !command.trimmed().isEmpty())
        QProcess::startDetached(QStringLiteral("/bin/sh"), {QStringLiteral("-c"), command}, QDir::homePath());
}

void ProcessesPage::showViewMenu()
{
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    auto *speed = menu->addMenu(tr("Update speed"));
    Ui::styleMenu(speed);
    const QList<QPair<QString, int>> speeds = {{tr("High"), 1000}, {tr("Normal"), 2000},
                                               {tr("Low"), 5000}, {tr("Paused"), 0}};
    for (const auto &[label, msec] : speeds) {
        QAction *action = speed->addAction(label, this, [this, msec = msec] { m_sampler->setInterval(msec); });
        action->setCheckable(true);
        action->setChecked(m_sampler->interval() == msec);
    }
    menu->addAction(tr("Refresh now"), m_sampler, &Sampler::refresh);
    menu->addSeparator();
    menu->addAction(tr("Expand all"), m_view, &QTreeView::expandAll);
    menu->addAction(tr("Collapse all"), this, [this] {
        m_view->collapseAll();
        for (int row = 0; row < m_model->rowCount(); ++row)
            m_view->expand(m_model->index(row, 0));
    });
    menu->popup(QCursor::pos());
}

void ProcessesPage::showContextMenu(const QPoint &pos)
{
    const QModelIndex index = m_view->indexAt(pos);
    if (!index.isValid() || index.data(ProcessModel::GroupRole).toBool())
        return;
    const QList<QVariant> pids = index.data(ProcessModel::PidsRole).toList();
    const int pid = pids.isEmpty() ? 0 : pids.first().toInt();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    Ui::styleMenu(menu);
    const auto processes = selectedProcesses();
    menu->addAction(tr("End task"), this, [this, processes] { applyAction(processes, SIGTERM); });
    menu->addAction(tr("End process tree"), this, [this, processes] { applyAction(processes, SIGKILL); });
    menu->addAction(tr("Efficiency mode"), this, [this, processes] { applyAction(processes, 0); });
    menu->addSeparator();
    const QString exe = QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).symLinkTarget();
    QAction *location = menu->addAction(tr("Open file location"), this, [this, exe] {
        Karyo::Apps::showInFiles(exe, this);
    });
    location->setEnabled(!exe.isEmpty());
    menu->addAction(tr("Copy PID"), this, [pid] { QApplication::clipboard()->setText(QString::number(pid)); });
    menu->popup(m_view->viewport()->mapToGlobal(pos));
}
