// SPDX-License-Identifier: GPL-3.0-or-later
#include "performancepage.h"

#include "processmodel.h"
#include "sampler.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QProcess>
#include <QStandardPaths>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

void drawSeries(QPainter &painter, const QRectF &area, const QList<double> &values, const QColor &color,
                qreal width, int capacity = 60)
{
    if (values.size() < 2)
        return;
    QPainterPath line;
    const qreal step = area.width() / (capacity - 1);
    const qreal startX = area.right() - step * (values.size() - 1);
    for (int i = 0; i < values.size(); ++i) {
        const QPointF p(startX + step * i, area.bottom() - qBound(0.0, values.at(i), 1.0) * area.height());
        i == 0 ? line.moveTo(p) : line.lineTo(p);
    }
    QPainterPath fill = line;
    fill.lineTo(area.right(), area.bottom());
    fill.lineTo(startX, area.bottom());
    fill.closeSubpath();
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillPath(fill, Ui::alpha(color, 22));
    painter.setPen(QPen(color, width, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(line);
    painter.restore();
}

QString uptimeText(qint64 seconds)
{
    return QStringLiteral("%1:%2:%3:%4")
        .arg(seconds / 86400)
        .arg((seconds / 3600) % 24, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString bitsText(double bytesPerSec)
{
    const double bits = bytesPerSec * 8;
    if (bits >= 1e9)
        return QStringLiteral("%1 Gb/s").arg(bits / 1e9, 0, 'f', 1);
    if (bits >= 1e6)
        return QStringLiteral("%1 Mb/s").arg(bits / 1e6, 0, 'f', bits >= 1e7 ? 0 : 1);
    return QStringLiteral("%1 Kb/s").arg(qRound(bits / 1e3));
}

} // namespace

struct Device
{
    QString kind;  // cpu, memory, disk:<name>, net:<name>
    QString title;
    QString line1;
    QString line2;
    QList<double> history;
    QColor color;
};

class DeviceList : public QWidget
{
    Q_OBJECT
public:
    explicit DeviceList(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setFixedWidth(280);
        setMouseTracking(true);
    }
    void setDevices(const QList<Device> &devices)
    {
        m_devices = devices;
        setMinimumHeight(int(devices.size()) * RowHeight);
        update();
    }
    const QList<Device> &devices() const { return m_devices; }
    int current() const { return m_current; }
    void setCurrent(int index)
    {
        m_current = index;
        update();
        Q_EMIT currentChanged(index);
    }

Q_SIGNALS:
    void currentChanged(int index);

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < m_devices.size(); ++i) {
            const Device &d = m_devices.at(i);
            const QRectF row(0, i * RowHeight, width(), RowHeight - 6);
            if (i == m_current)
                Ui::fillRounded(painter, row, 8, t.sel);
            else if (i == m_hover)
                Ui::fillRounded(painter, row, 8, t.hover);
            const QRectF spark(12, row.top() + (row.height() - 46) / 2, 72, 46);
            Ui::fillRounded(painter, spark, 10, Ui::alpha(d.color, 12));
            painter.save();
            QPainterPath clip;
            clip.addRoundedRect(spark, 10, 10);
            painter.setClipPath(clip);
            drawSeries(painter, spark.adjusted(0, 6, 0, -2), d.history, d.color, 1.6);
            painter.restore();
            const int x = 98;
            painter.setPen(t.text);
            painter.setFont(Ui::bodyFont(13.5, QFont::DemiBold));
            painter.drawText(QRectF(x, row.top() + 10, width() - x - 8, 20), Qt::AlignLeft | Qt::AlignVCenter, d.title);
            painter.setFont(Ui::bodyFont(12));
            painter.setPen(t.muted);
            painter.drawText(QRectF(x, row.top() + 29, width() - x - 8, 16), Qt::AlignLeft | Qt::AlignVCenter,
                             painter.fontMetrics().elidedText(d.line1, Qt::ElideRight, width() - x - 8));
            if (!d.line2.isEmpty()) {
                painter.setPen(i == m_current ? t.ink1 : t.muted);
                painter.drawText(QRectF(x, row.top() + 45, width() - x - 8, 16), Qt::AlignLeft | Qt::AlignVCenter,
                                 d.line2);
            }
        }
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        const int index = int(event->position().y()) / RowHeight;
        if (index >= 0 && index < m_devices.size())
            setCurrent(index);
    }
    void mouseMoveEvent(QMouseEvent *event) override
    {
        const int index = int(event->position().y()) / RowHeight;
        const int hover = index < m_devices.size() ? index : -1;
        if (hover != m_hover) {
            m_hover = hover;
            update();
        }
    }
    void leaveEvent(QEvent *) override
    {
        m_hover = -1;
        update();
    }

private:
    static constexpr int RowHeight = 76;
    QList<Device> m_devices;
    int m_current = 0;
    int m_hover = -1;
};

class Chart : public QWidget
{
public:
    explicit Chart(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(220);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    void setSeries(const QList<double> &values, const QColor &color)
    {
        m_values = values;
        m_color = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        const QRectF area = rect();
        Ui::fillRounded(painter, area, 8, Ui::alpha(m_color, 14));
        painter.save();
        QPainterPath clip;
        clip.addRoundedRect(area, 16, 16);
        painter.setClipPath(clip);
        for (int i = 1; i < 4; ++i)
            painter.fillRect(QRectF(0, area.height() * i / 4.0, area.width(), 1), Ui::alpha(m_color, 22));
        drawSeries(painter, area, m_values, m_color, 2.5);
        painter.restore();
    }

private:
    QList<double> m_values;
    QColor m_color;
};

PerformancePage::PerformancePage(Sampler *sampler, QWidget *parent)
    : QWidget(parent)
    , m_sampler(sampler)
    , m_devices(new DeviceList)
    , m_chart(new Chart)
    , m_title(new QLabel)
    , m_subtitle(new QLabel)
    , m_caption(new QLabel)
    , m_stats(new QGridLayout)
{
    const Ui::Theme &t = Ui::Theme::current();
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 22, 22, 18);
    layout->setSpacing(14);

    auto *top = new QHBoxLayout;
    auto *heading = new QLabel(tr("Performance"));
    heading->setFont(Ui::headingFont(26));
    top->addWidget(heading);
    top->addStretch();
    if (!QStandardPaths::findExecutable(QStringLiteral("htop")).isEmpty()) {
        auto *htop = new Ui::PillButton(QStringLiteral("square-terminal"), QStringLiteral("htop"));
        connect(htop, &QAbstractButton::clicked, this, [] {
            QProcess::startDetached(QStringLiteral("x-terminal-emulator"), {QStringLiteral("-e"), QStringLiteral("htop")});
        });
        top->addWidget(htop);
    }
    layout->addLayout(top);

    auto *body = new QHBoxLayout;
    body->setSpacing(20);
    auto *devicesColumn = new QVBoxLayout;
    devicesColumn->addWidget(m_devices);
    devicesColumn->addStretch();
    body->addLayout(devicesColumn);

    auto *detail = new QVBoxLayout;
    detail->setSpacing(8);
    auto *titleRow = new QHBoxLayout;
    m_title->setFont(Ui::headingFont(30));
    m_subtitle->setFont(Ui::bodyFont(13.5));
    QPalette muted = m_subtitle->palette();
    muted.setColor(QPalette::WindowText, t.muted);
    m_subtitle->setPalette(muted);
    titleRow->addWidget(m_title);
    titleRow->addStretch();
    titleRow->addWidget(m_subtitle, 0, Qt::AlignBottom);
    detail->addLayout(titleRow);
    m_caption->setFont(Ui::bodyFont(12));
    m_caption->setPalette(muted);
    detail->addWidget(m_caption);
    detail->addWidget(m_chart, 1);
    auto *axis = new QLabel(tr("60 seconds"));
    axis->setFont(Ui::bodyFont(12));
    axis->setPalette(muted);
    detail->addWidget(axis);
    m_stats->setHorizontalSpacing(36);
    m_stats->setVerticalSpacing(14);
    auto *statsRow = new QHBoxLayout;
    statsRow->addLayout(m_stats);
    statsRow->addStretch();
    detail->addSpacing(10);
    detail->addLayout(statsRow);
    body->addLayout(detail, 1);
    layout->addLayout(body, 1);

    connect(m_devices, &DeviceList::currentChanged, this, &PerformancePage::refresh);
    connect(sampler, &Sampler::updated, this, &PerformancePage::refresh);
    refresh();
}

void PerformancePage::setStats(const QList<QPair<QString, QString>> &stats)
{
    while (QLayoutItem *item = m_stats->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const Ui::Theme &t = Ui::Theme::current();
    for (int i = 0; i < stats.size(); ++i) {
        auto *cell = new QWidget;
        auto *v = new QVBoxLayout(cell);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(0);
        auto *label = new QLabel(stats.at(i).first);
        label->setFont(Ui::bodyFont(12));
        QPalette muted = label->palette();
        muted.setColor(QPalette::WindowText, t.muted);
        label->setPalette(muted);
        auto *value = new QLabel(stats.at(i).second);
        value->setFont(Ui::bodyFont(24, QFont::DemiBold));
        v->addWidget(label);
        v->addWidget(value);
        m_stats->addWidget(cell, i / 4, i % 4);
    }
}

void PerformancePage::refresh()
{
    const Ui::Theme &t = Ui::Theme::current();
    const Sampler &s = *m_sampler;
    QList<Device> devices;
    devices.append({QStringLiteral("cpu"), tr("CPU"), s.cpuModel(),
                    QStringLiteral("%1% · %2 GHz").arg(qRound(s.cpuPercent())).arg(s.cpuMhz() / 1000.0, 0, 'f', 2),
                    s.cpuHistory().values, t.accent});
    const double usedGb = (s.memTotal() - s.memAvailable()) / 1073741824.0;
    devices.append({QStringLiteral("memory"), tr("Memory"),
                    QStringLiteral("%1 / %2 GB (%3%)").arg(usedGb, 0, 'f', 1).arg(s.memTotal() / 1073741824.0, 0, 'f', 1)
                        .arg(qRound(s.memoryPercent())),
                    QString(), s.memoryHistory().values, t.accent2});
    int diskIndex = 0;
    for (const DiskInfo &disk : s.disks()) {
        const QString where = disk.mountPoint.isEmpty() ? disk.name : disk.mountPoint;
        devices.append({QStringLiteral("disk:") + disk.name,
                        tr("Disk %1 (%2)").arg(diskIndex++).arg(where == QLatin1String("/") ? disk.name : where),
                        QStringLiteral("%1 · %2%").arg(disk.rotational ? tr("HDD") : tr("SSD")).arg(qRound(disk.busyPercent)),
                        QString(), disk.history.values, t.accent2});
    }
    for (const NetInfo &net : s.interfaces()) {
        const bool wifi = net.name.startsWith(QLatin1String("wl"));
        devices.append({QStringLiteral("net:") + net.name, wifi ? tr("Wi-Fi") : tr("Ethernet"),
                        QStringLiteral("%1 · ↓ %2").arg(net.name, bitsText(net.rxBytesPerSec)), QString(),
                        net.history.values, t.accent});
    }
    const int current = qBound(0, m_devices->current(), int(devices.size()) - 1);
    m_devices->setDevices(devices);
    const Device &d = devices.at(current);
    m_chart->setSeries(d.history, d.color);
    m_title->setText(d.title);

    if (d.kind == QLatin1String("cpu")) {
        m_subtitle->setText(tr("%1 · %2 cores").arg(s.cpuModel()).arg(s.cpuCount()));
        m_caption->setText(tr("% Utilization over 60 seconds"));
        setStats({{tr("Utilization"), QStringLiteral("%1%").arg(qRound(s.cpuPercent()))},
                  {tr("Speed"), QStringLiteral("%1 GHz").arg(s.cpuMhz() / 1000.0, 0, 'f', 2)},
                  {tr("Processes"), QString::number(s.processes().size())},
                  {tr("Threads"), QLocale().toString(s.threadCount())},
                  {tr("Load average"), QStringLiteral("%1 · %2 · %3").arg(s.loadAverage(0), 0, 'f', 1)
                                           .arg(s.loadAverage(1), 0, 'f', 1).arg(s.loadAverage(2), 0, 'f', 1)},
                  {tr("Up time"), uptimeText(s.uptimeSeconds())}});
    } else if (d.kind == QLatin1String("memory")) {
        m_subtitle->setText(tr("%1 GB total").arg(s.memTotal() / 1073741824.0, 0, 'f', 1));
        m_caption->setText(tr("Memory usage over 60 seconds"));
        setStats({{tr("In use"), formatBytes(double(s.memTotal() - s.memAvailable()))},
                  {tr("Available"), formatBytes(double(s.memAvailable()))},
                  {tr("Cached"), formatBytes(double(s.memCached()))},
                  {tr("Swap used"), s.swapTotal() ? formatBytes(double(s.swapTotal() - s.swapFree())) : tr("None")}});
    } else if (d.kind.startsWith(QLatin1String("disk:"))) {
        for (const DiskInfo &disk : s.disks()) {
            if (QStringLiteral("disk:") + disk.name != d.kind)
                continue;
            m_subtitle->setText(disk.name);
            m_caption->setText(tr("Active time over 60 seconds"));
            setStats({{tr("Active time"), QStringLiteral("%1%").arg(qRound(disk.busyPercent))},
                      {tr("Read speed"), formatRate(disk.readBytesPerSec)},
                      {tr("Write speed"), formatRate(disk.writeBytesPerSec)},
                      {tr("Type"), disk.rotational ? tr("HDD") : tr("SSD")}});
        }
    } else {
        for (const NetInfo &net : s.interfaces()) {
            if (QStringLiteral("net:") + net.name != d.kind)
                continue;
            m_subtitle->setText(net.name);
            m_caption->setText(net.linkMbps ? tr("Throughput over 60 seconds, of %1 Mb/s").arg(net.linkMbps)
                                            : tr("Throughput over 60 seconds"));
            setStats({{tr("Receive"), bitsText(net.rxBytesPerSec)},
                      {tr("Send"), bitsText(net.txBytesPerSec)},
                      {tr("Link speed"), net.linkMbps ? tr("%1 Mb/s").arg(net.linkMbps) : tr("Unknown")}});
        }
    }
}

#include "performancepage.moc"
