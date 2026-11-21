// SPDX-License-Identifier: GPL-3.0-or-later
// Performance page (design 1l): devices with sparklines, a 60-second chart
// and the selected device's figures.
#pragma once

#include <QWidget>

class QGridLayout;
class QLabel;
class Sampler;
class DeviceList;
class Chart;

class PerformancePage : public QWidget
{
    Q_OBJECT
public:
    PerformancePage(Sampler *sampler, QWidget *parent = nullptr);

private:
    void refresh();
    void setStats(const QList<QPair<QString, QString>> &stats);

    Sampler *m_sampler;
    DeviceList *m_devices;
    Chart *m_chart;
    QLabel *m_title;
    QLabel *m_subtitle;
    QLabel *m_caption;
    QGridLayout *m_stats;
};
