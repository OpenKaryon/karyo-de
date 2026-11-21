// SPDX-License-Identifier: GPL-3.0-or-later
// System: displays (wlr-randr), sound output and volume (wpctl),
// notifications (the shell's Do not disturb) and a way into Storage.
#include "config.h"
#include "pages.h"
#include "widgets.h"

#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>

#include <QComboBox>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPainter>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

QDBusMessage shellCall(const QString &method, const QVariantList &args = { })
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.karyo.Shell"),
        QStringLiteral("/org/karyo/Shell"), QStringLiteral("org.karyo.Shell"), method);
    call.setArguments(args);
    return call;
}

class SystemPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit SystemPage(QWidget *parent)
        : SettingsPage(tr("System"), parent)
    {
        addKeywords({ tr("Display"), tr("Resolution"), tr("Scale"), tr("Sound"), tr("Volume"), tr("Storage"),
            tr("Notifications"), QStringLiteral("display"), QStringLiteral("sound") });
        column()->addWidget(sectionHeading(tr("Display")));
        m_displayStatus = new QLabel(this);
        m_displayStatus->setObjectName("displayStatus");
        m_displayStatus->setWordWrap(true);
        m_displayStatus->setTextFormat(Qt::PlainText);
        column()->addWidget(m_displayStatus);
        m_displayControls = new QWidget(this);
        m_displays = new QVBoxLayout(m_displayControls);
        m_displays->setContentsMargins(0, 0, 0, 0);
        m_displays->setSpacing(10);
        column()->addWidget(m_displayControls);

        column()->addWidget(sectionHeading(tr("Sound")));
        m_output = new SettingRow(QStringLiteral("speaker"), tr("Output"), tr("Where sound plays"), this);
        m_outputs = settingCombo();
        m_outputs->setObjectName("audioOutput");
        m_outputs->setEnabled(false);
        m_output->setControl(m_outputs);
        column()->addWidget(m_output);
        m_volumeRow = new SettingRow(QStringLiteral("volume-2"), tr("Volume"), QString(), this);
        m_volume = new QSlider(Qt::Horizontal);
        m_volume->setObjectName("audioVolume");
        m_volume->setEnabled(false);
        m_volume->setRange(0, 100);
        m_volume->setFixedWidth(260);
        m_volumeRow->setControl(m_volume);
        column()->addWidget(m_volumeRow);
        m_soundStatus = new QLabel(this);
        m_soundStatus->setObjectName("soundStatus");
        m_soundStatus->setWordWrap(true);
        m_soundStatus->setTextFormat(Qt::PlainText);
        column()->addWidget(m_soundStatus);
        m_volumeTimer = new QTimer(this);
        m_volumeTimer->setSingleShot(true);
        m_volumeTimer->setInterval(120);
        connect(m_volume, &QSlider::valueChanged, this,
            [this](int value)
            {
                m_volumeRow->setValue(QStringLiteral("%1%").arg(value));
                m_volumeTimer->start();
            });
        connect(m_volumeTimer, &QTimer::timeout, this,
            [this]
            {
                m_volumeBusy = true;
                m_volume->setEnabled(false);
                m_soundStatus->clear();
                runCommand(
                    this, QStringLiteral("wpctl"),
                    { QStringLiteral("set-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@"),
                        QString::number(m_volume->value() / 100.0, 'f', 2) },
                    [this](const QString &)
                    {
                        m_volumeBusy = false;
                        readVolume();
                    },
                    [this](const QString &error)
                    {
                        m_volumeBusy = false;
                        m_soundStatus->setText(error);
                        readVolume();
                    });
            });
        connect(m_outputs, &QComboBox::activated, this,
            [this](int index)
            {
                m_outputsBusy = true;
                m_outputs->setEnabled(false);
                m_soundStatus->clear();
                runCommand(
                    this, QStringLiteral("wpctl"),
                    { QStringLiteral("set-default"), m_outputs->itemData(index).toString() },
                    [this](const QString &)
                    {
                        m_outputsBusy = false;
                        readOutputs();
                    },
                    [this](const QString &error)
                    {
                        m_outputsBusy = false;
                        m_soundStatus->setText(error);
                        readOutputs();
                    });
            });

        column()->addWidget(sectionHeading(tr("Notifications")));
        auto *dnd = new SettingRow(QStringLiteral("bell-off"), tr("Do not disturb"),
            tr("Notifications go straight to the Notification center"), this);
        m_dnd = dnd->addToggle(false);
        m_dnd->setObjectName("doNotDisturb");
        m_dnd->setEnabled(false);
        connect(m_dnd, &QAbstractButton::toggled, this,
            [this](bool on)
            {
                m_dndBusy = true;
                m_dnd->setEnabled(false);
                m_dndStatus->clear();
                auto *watcher = new QDBusPendingCallWatcher(
                    QDBusConnection::sessionBus().asyncCall(
                        shellCall(QStringLiteral("SetDoNotDisturb"), { on }), 5000),
                    this);
                connect(watcher, &QDBusPendingCallWatcher::finished, this,
                    [this, watcher]
                    {
                        const QDBusPendingReply<> reply = *watcher;
                        watcher->deleteLater();
                        if (reply.isError())
                            m_dndStatus->setText(reply.error().message());
                        m_dndBusy = false;
                        readDnd(true);
                    });
            });
        column()->addWidget(dnd);
        m_dndStatus = new QLabel(this);
        m_dndStatus->setObjectName("dndStatus");
        m_dndStatus->setWordWrap(true);
        m_dndStatus->setTextFormat(Qt::PlainText);
        column()->addWidget(m_dndStatus);

        column()->addWidget(sectionHeading(tr("Storage")));
        const QStorageInfo system(QStringLiteral("/"));
        auto *storage = new SettingRow(QStringLiteral("hard-drive"), tr("Storage"),
            tr("Disks, what fills them and what can be cleaned up"), this);
        storage->setLink(system.isValid()
                ? tr("%1 free").arg(QLocale().formattedDataSize(
                      system.bytesAvailable(), 1, QLocale::DataSizeTraditionalFormat))
                : QString());
        connect(storage, &SettingRow::clicked, this,
            [this] { Q_EMIT subPageRequested(createStoragePage(nullptr)); });
        column()->addWidget(storage);
        QTimer::singleShot(0, this, [this] { refresh(); });
    }

    void refresh() override
    {
        readDisplays();
        readVolume();
        readOutputs();
        readDnd();
    }

private:
    void readDnd(bool preserveError = false)
    {
        if (m_dndBusy)
            return;
        m_dndBusy = true;
        if (!preserveError)
            m_dndStatus->clear();
        m_dnd->setEnabled(false);
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(shellCall(QStringLiteral("DoNotDisturb")), 5000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher]
            {
                const QDBusPendingReply<bool> reply = *watcher;
                watcher->deleteLater();
                m_dndBusy = false;
                if (reply.isError())
                {
                    m_dndStatus->setText(
                        tr("Notifications are unavailable: %1").arg(reply.error().message()));
                    return;
                }
                const QSignalBlocker blocked(m_dnd);
                m_dnd->setChecked(reply.value());
                m_dnd->setEnabled(true);
            });
    }
    void readDisplays(bool preserveError = false)
    {
        if (m_displayBusy)
            return;
        m_displayBusy = true;
        if (!preserveError)
            m_displayStatus->clear();
        m_displayControls->setEnabled(false);
        runCommand(
            this, QStringLiteral("wlr-randr"), { QStringLiteral("--json") },
            [this](const QString &out)
            {
                m_displayBusy = false;
                QJsonParseError error;
                const auto document = QJsonDocument::fromJson(out.toUtf8(), &error);
                if (error.error != QJsonParseError::NoError || !document.isArray())
                {
                    m_displayStatus->setText(tr("The compositor returned an invalid display list."));
                    return;
                }
                while (QLayoutItem *item = m_displays->takeAt(0))
                {
                    delete item->widget();
                    delete item;
                }
                const auto outputs = document.array();
                int enabled = 0;
                for (const QJsonValue &value : outputs)
                {
                    const auto output = value.toObject();
                    if (output.value(QStringLiteral("enabled")).toBool())
                    {
                        addDisplay(output);
                        ++enabled;
                    }
                }
                m_displayControls->setEnabled(true);
                if (!enabled)
                    m_displayStatus->setText(tr("No enabled displays were reported."));
            },
            [this](const QString &error)
            {
                m_displayBusy = false;
                m_displayStatus->setText(error);
            });
    }

    void addDisplay(const QJsonObject &output)
    {
        const QString name = output.value(QStringLiteral("name")).toString();
        QString title = (output.value(QStringLiteral("make")).toString() + QLatin1Char(' ')
            + output.value(QStringLiteral("model")).toString())
                            .trimmed();
        if (title.isEmpty() || title == QLatin1String("headless"))
            title = name;
        auto *resolution = new SettingRow(
            QStringLiteral("monitor"), tr("Display resolution"), title + QStringLiteral(" · ") + name, this);
        auto *modes = settingCombo();
        modes->setObjectName("displayMode");
        modes->setMinimumWidth(320);
        QStringList seen;
        for (const QJsonValue &m : output.value(QStringLiteral("modes")).toArray())
        {
            const QJsonObject mode = m.toObject();
            const int w = mode.value(QStringLiteral("width")).toInt();
            const int h = mode.value(QStringLiteral("height")).toInt();
            const double refresh = mode.value(QStringLiteral("refresh")).toDouble();
            const QString key = QStringLiteral("%1x%2@%3").arg(w).arg(h).arg(refresh, 0, 'f', 3);
            const QString label = QStringLiteral("%1 × %2 · %3 Hz").arg(w).arg(h).arg(refresh, 0, 'f', 2);
            if (seen.contains(key))
                continue;
            seen.append(key);
            modes->addItem(
                mode.value(QStringLiteral("preferred")).toBool() ? label + tr(" (recommended)") : label, key);
            if (mode.value(QStringLiteral("current")).toBool())
                modes->setCurrentIndex(modes->count() - 1);
        }
        connect(modes, &QComboBox::activated, this,
            [this, name, modes](int index)
            {
                apply(name, { QStringLiteral("--mode"), modes->itemData(index).toString() },
                    QStringLiteral("mode"), modes->itemData(index).toString());
            });
        resolution->setControl(modes);
        m_displays->addWidget(resolution);

        auto *scale = new SettingRow(
            QStringLiteral("type"), tr("Scale"), tr("Size of text, apps and other items"), this);
        auto *scales = settingCombo();
        scales->setObjectName("displayScale");
        const double current = output.value(QStringLiteral("scale")).toDouble(1.0);
        for (double s : { 1.0, 1.25, 1.5, 1.75, 2.0 })
        {
            scales->addItem(QStringLiteral("%1%").arg(qRound(s * 100)), s);
            if (qAbs(s - current) < 0.01)
                scales->setCurrentIndex(scales->count() - 1);
        }
        if (scales->findData(current) < 0)
        {
            scales->addItem(QStringLiteral("%1%").arg(qRound(current * 100)), current);
            scales->setCurrentIndex(scales->count() - 1);
        }
        connect(scales, &QComboBox::activated, this,
            [this, name, scales](int index)
            {
                const QString value = QString::number(scales->itemData(index).toDouble());
                apply(name, { QStringLiteral("--scale"), value }, QStringLiteral("scale"), value);
            });
        scale->setControl(scales);
        m_displays->addWidget(scale);
    }

    // Save a sign-in preference only after the compositor accepted the change.
    void apply(const QString &output, const QStringList &args, const QString &key, const QString &value)
    {
        if (m_displayBusy)
            return;
        m_displayBusy = true;
        m_displayControls->setEnabled(false);
        m_displayStatus->clear();
        runCommand(
            this, QStringLiteral("wlr-randr"), QStringList { QStringLiteral("--output"), output } + args,
            [this, output, key, value](const QString &)
            {
                auto settings = Config::desktop();
                settings.setValue(QStringLiteral("Displays/%1/%2").arg(output, key), value);
                settings.sync();
                if (settings.status() != QSettings::NoError)
                    m_displayStatus->setText(
                        tr("The display changed, but its sign-in preference could not be saved."));
                m_displayBusy = false;
                readDisplays(true);
            },
            [this](const QString &error)
            {
                m_displayBusy = false;
                m_displayStatus->setText(error);
                readDisplays(true);
            });
    }

    void readVolume()
    {
        if (m_volumeBusy || m_volumeTimer->isActive())
            return;
        m_volumeBusy = true;
        m_volume->setEnabled(false);
        runCommand(
            this, QStringLiteral("wpctl"),
            { QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@") },
            [this](const QString &out)
            {
                m_volumeBusy = false;
                const auto match = QRegularExpression(QStringLiteral("Volume:\\s*([0-9.]+)")).match(out);
                bool valid = false;
                const double volume = match.captured(1).toDouble(&valid);
                if (!match.hasMatch() || !valid)
                {
                    m_soundStatus->setText(tr("The audio server returned an invalid volume."));
                    return;
                }
                const QSignalBlocker blocked(m_volume);
                m_volume->setValue(qRound(volume * 100));
                m_volume->setEnabled(true);
                m_volumeRow->setValue(QStringLiteral("%1%").arg(m_volume->value()));
            },
            [this](const QString &error)
            {
                m_volumeBusy = false;
                m_soundStatus->setText(error);
            });
    }
    void readOutputs()
    {
        if (m_outputsBusy)
            return;
        m_outputsBusy = true;
        m_outputs->setEnabled(false);
        // Sinks in "wpctl status": " │  *   45. Built-in Audio Analog Stereo [vol: 0.40]"
        runCommand(
            this, QStringLiteral("wpctl"), { QStringLiteral("status") },
            [this](const QString &out)
            {
                m_outputsBusy = false;
                m_outputs->clear();
                bool inSinks = false;
                const QRegularExpression line(
                    QStringLiteral("^[\\s│├└─]*(\\*)?\\s*(\\d+)\\.\\s+(.+?)(\\s+\\[vol:.*\\])?$"));
                for (const QString &raw : out.split(QLatin1Char('\n')))
                {
                    if (raw.contains(QLatin1String("Sinks:")))
                    {
                        inSinks = true;
                        continue;
                    }
                    if (inSinks
                        && (raw.contains(QLatin1String("Sources:")) || raw.trimmed() == QLatin1String("│")))
                        break;
                    if (!inSinks)
                        continue;
                    const auto match = line.match(raw);
                    if (!match.hasMatch())
                        continue;
                    m_outputs->addItem(match.captured(3).trimmed(), match.captured(2));
                    if (!match.captured(1).isEmpty())
                        m_outputs->setCurrentIndex(m_outputs->count() - 1);
                }
                m_outputs->setEnabled(m_outputs->count() > 0);
                if (m_outputs->count() == 0)
                    m_soundStatus->setText(tr("No audio outputs are available."));
            },
            [this](const QString &error)
            {
                m_outputsBusy = false;
                m_soundStatus->setText(error);
            });
    }

    bool m_displayBusy = false, m_volumeBusy = false, m_outputsBusy = false, m_dndBusy = false;
    QWidget *m_displayControls;
    QLabel *m_displayStatus, *m_soundStatus, *m_dndStatus;
    QTimer *m_volumeTimer;
    ToggleSwitch *m_dnd;
    QVBoxLayout *m_displays;
    SettingRow *m_output;
    QComboBox *m_outputs;
    SettingRow *m_volumeRow;
    QSlider *m_volume;
};

} // namespace

SettingsPage *createSystemPage(QWidget *parent) { return new SystemPage(parent); }

#include "system.moc"
