// SPDX-License-Identifier: GPL-3.0-or-later
// Updates (apt, read without root from its cached lists) and About Karyo.
#include "config.h"
#include "pages.h"
#include "softwarejob.h"
#include "widgets.h"

#include <QClipboard>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QLocale>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QSysInfo>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

namespace
{

struct Upgrade
{
    QString package;
    QString from;
    QString to;
};

// "Inst libfoo [1.0-1] (1.0-2 Debian-Security:13/stable-security [amd64])"
QList<Upgrade> parseSimulation(const QString &out)
{
    QList<Upgrade> upgrades;
    const QRegularExpression inst(QStringLiteral("^Inst (\\S+) (?:\\[([^\\]]+)\\] )?\\((\\S+)"));
    for (const QString &line : out.split(QLatin1Char('\n')))
    {
        const auto match = inst.match(line);
        if (match.hasMatch())
            upgrades.append({ match.captured(1), match.captured(2), match.captured(3) });
    }
    return upgrades;
}

void simulateUpgrade(QObject *context, const std::function<void(const QList<Upgrade> &, bool)> &done)
{
    // Read-only, bounded query; this must never share the transaction lifetime.
    runCommand(
        context, QStringLiteral("apt-get"),
        { QStringLiteral("-s"), QStringLiteral("-o"), QStringLiteral("Debug::NoLocking=1"),
            QStringLiteral("dist-upgrade") },
        [done](const QString &out) { done(parseSimulation(out), true); },
        [done](const QString &) { done({ }, false); }, 60000);
}

QString lastChecked()
{
    QDateTime newest;
    const QFileInfoList lists = QDir(QStringLiteral("/var/lib/apt/lists"))
                                    .entryInfoList({ QStringLiteral("*Release") }, QDir::Files);
    for (const QFileInfo &file : lists)
    {
        if (!newest.isValid() || file.lastModified() > newest)
            newest = file.lastModified();
    }
    return newest.isValid() ? QLocale().toString(newest, QLocale::ShortFormat) : QString();
}

class UpdatesPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit UpdatesPage(QWidget *parent)
        : SettingsPage(tr("Updates"), parent)
    {
        addKeywords({ QStringLiteral("apt"), QStringLiteral("upgrade"), tr("Check for updates") });
        m_status
            = new SettingRow(QStringLiteral("refresh-cw"), tr("System and Karyo packages"), QString(), this);
        m_status->setObjectName("packageUpdateStatus");
        m_status->setValue(tr("Checking…"));
        auto *check = new QPushButton(tr("Check for updates"));
        check->setObjectName("refreshPackageLists");
        connect(check, &QPushButton::clicked, this,
            [this, check]
            {
                if (m_checking || Karyo::Apps::SoftwareJob::hasActiveJobs())
                {
                    m_status->setDescription(tr("Another package operation is still running."));
                    return;
                }
                ++m_generation;
                m_reading = false;
                m_checking = true;
                check->setEnabled(false);
                m_updateError.clear();
                m_status->setDescription(tr("Downloading package lists…"));
                auto *job = Karyo::Apps::SoftwareJob::startApt(QStringLiteral("update"));
                connect(job, &Karyo::Apps::SoftwareJob::finished, this,
                    [this, check](bool success, const QString &error)
                    {
                        m_checking = false;
                        check->setEnabled(true);
                        if (!success)
                        {
                            m_updateError = error;
                            m_status->setValue(tr("Could not update package lists"));
                            m_status->setDescription(error);
                        }
                        else
                            refresh();
                    });
            });
        m_status->setControl(check);
        m_install = new QPushButton(tr("Open in App Center"));
        m_install->hide();
        // App Center installs them with progress.
        connect(m_install, &QPushButton::clicked, this,
            [this]
            {
                if (!QProcess::startDetached(
                        QStringLiteral("karyo-appcenter"), { QStringLiteral("updates") }))
                    m_status->setDescription(tr(
                        "App Center could not be started. Install the optional Karyo App Center package."));
            });
        m_status->setControl(m_install);
        column()->addWidget(m_status);
        m_list = new QVBoxLayout;
        m_list->setSpacing(10);
        column()->addLayout(m_list);
        QTimer::singleShot(0, this, [this] { refresh(); });
    }

    void refresh() override
    {
        if (m_checking || m_reading)
            return;
        m_reading = true;
        const auto generation = m_generation;
        simulateUpgrade(this,
            [this, generation](const QList<Upgrade> &upgrades, bool ok)
            {
                if (generation != m_generation)
                    return;
                m_reading = false;
                while (QLayoutItem *item = m_list->takeAt(0))
                {
                    delete item->widget();
                    delete item;
                }
                const QString checked = lastChecked();
                m_status->setDescription(!m_updateError.isEmpty() ? m_updateError
                        : checked.isEmpty()                       ? QString()
                                                                  : tr("Last checked: %1").arg(checked));
                m_install->setVisible(!upgrades.isEmpty());
                if (!ok)
                {
                    m_status->setValue(tr("apt could not tell"));
                    return;
                }
                m_status->setValue(upgrades.isEmpty()
                        ? tr("You're up to date")
                        : tr("%n updates available", nullptr, int(upgrades.size())));
                for (const Upgrade &upgrade : upgrades.mid(0, 60))
                {
                    m_list->addWidget(new SettingRow(QStringLiteral("package"), upgrade.package,
                        upgrade.from.isEmpty() ? tr("New: %1").arg(upgrade.to)
                                               : upgrade.from + QStringLiteral(" → ") + upgrade.to,
                        this));
                }
            });
    }

private:
    quint64 m_generation = 0;
    bool m_checking = false, m_reading = false;
    QString m_updateError;
    SettingRow *m_status;
    QPushButton *m_install;
    QVBoxLayout *m_list;
};

QString readFirst(const QString &path, const QRegularExpression &pattern)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return { };
    const auto match = pattern.match(QString::fromUtf8(file.readAll()));
    return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

class AboutPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit AboutPage(QWidget *parent)
        : SettingsPage(tr("About Karyo"), parent)
    {
        addKeywords({ tr("Device name"), tr("Processor"), tr("Version"), QStringLiteral("hostname") });
        m_name
            = new SettingRow(QStringLiteral("monitor"), QSysInfo::machineHostName(), tr("Device name"), this);
        auto *rename = m_rename = new QPushButton(tr("Rename this PC"));
        m_rename->setObjectName("renameComputer");
        connect(rename, &QPushButton::clicked, this, &AboutPage::rename);
        m_name->setControl(rename);
        column()->addWidget(m_name);

        column()->addWidget(sectionHeading(tr("Device specifications")));
        const QLocale locale;
        const QString cpu = readFirst(
            QStringLiteral("/proc/cpuinfo"), QRegularExpression(QStringLiteral("model name\\s*:\\s*(.+)")));
        const qint64 memKb = readFirst(
            QStringLiteral("/proc/meminfo"), QRegularExpression(QStringLiteral("MemTotal:\\s*(\\d+)")))
                                 .toLongLong();
        const QStorageInfo root = QStorageInfo::root();
        QString graphics;
        for (const QFileInfo &card :
            QDir(QStringLiteral("/sys/class/drm")).entryInfoList({ QStringLiteral("card?") }))
        {
            const QString driver
                = QFileInfo(card.filePath() + QStringLiteral("/device/driver")).symLinkTarget();
            if (!driver.isEmpty())
            {
                graphics = QFileInfo(driver).fileName();
                break;
            }
        }
        m_device = new FactsCard(this);
        m_device->setFacts(
            { { tr("Processor"), tr("%1 · %n cores", nullptr, QThread::idealThreadCount()).arg(cpu) },
                { tr("Installed RAM"), locale.formattedDataSize(memKb * 1024) },
                { tr("Storage"),
                    tr("%1 total, %2 free")
                        .arg(locale.formattedDataSize(root.bytesTotal()),
                            locale.formattedDataSize(root.bytesAvailable())) },
                { tr("Graphics"), graphics.isEmpty() ? tr("Unknown") : graphics },
                { tr("System type"), QSysInfo::currentCpuArchitecture() } });
        column()->addWidget(m_device);

        column()->addWidget(sectionHeading(tr("Karyo specifications")));
        const QString edition = readFirst(QStringLiteral("/etc/os-release"),
            QRegularExpression(QStringLiteral("PRETTY_NAME=\"?([^\"\\n]+)")));
        m_system = new FactsCard(this);
        m_facts = { { tr("Edition"), edition }, { tr("Desktop"), tr("Karyo shell on labwc") },
            { tr("Kernel"), QSysInfo::kernelType() + QLatin1Char(' ') + QSysInfo::kernelVersion() } };
        m_system->setFacts(m_facts);
        column()->addWidget(m_system);
        runCommand(this, QStringLiteral("dpkg-query"),
            { QStringLiteral("-W"), QStringLiteral("-f=${Version}"), QStringLiteral("karyo-shell") },
            [this](const QString &version)
            {
                m_facts.insert(1, { tr("Version"), version.trimmed() });
                m_system->setFacts(m_facts);
            });

        auto *copy = new SettingRow(QStringLiteral("copy"), tr("Copy specifications"),
            tr("For a support request or a lab inventory"), this);
        auto *copyButton = new QPushButton(tr("Copy"));
        connect(copyButton, &QPushButton::clicked, this,
            [this, cpu, memKb, edition, copy]
            {
                QStringList lines { tr("Device name") + QStringLiteral(": ") + QSysInfo::machineHostName() };
                for (const auto &fact : std::as_const(m_facts))
                    lines.append(fact.first + QStringLiteral(": ") + fact.second);
                lines.append(tr("Processor") + QStringLiteral(": ") + cpu);
                lines.append(
                    tr("Installed RAM") + QStringLiteral(": ") + QLocale().formattedDataSize(memKb * 1024));
                QGuiApplication::clipboard()->setText(lines.join(QLatin1Char('\n')));
                copy->setValue(tr("Copied"));
            });
        copy->setControl(copyButton);
        column()->addWidget(copy);
    }

private:
    void rename()
    {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tr("Rename this PC"),
            tr("Letters, digits and hyphens; it shows on the network"), QLineEdit::Normal,
            QSysInfo::machineHostName(), &ok)
                                 .trimmed();
        if (!ok || name.isEmpty()
            || !QRegularExpression(QRegularExpression::anchoredPattern(
                                       QStringLiteral("[A-Za-z0-9](?:[A-Za-z0-9-]{0,61}[A-Za-z0-9])?")))
                .match(name)
                .hasMatch())
            return;
        m_rename->setEnabled(false);
        const auto complete = [this](const QString &error)
        {
            auto call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.hostname1"),
                QStringLiteral("/org/freedesktop/hostname1"),
                QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
            call << QStringLiteral("org.freedesktop.hostname1") << QStringLiteral("StaticHostname");
            auto *watcher
                = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 5000), this);
            connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, watcher, error]
                {
                    const QDBusPendingReply<QDBusVariant> reply = *watcher;
                    watcher->deleteLater();
                    m_rename->setEnabled(true);
                    if (reply.isError())
                        m_name->setDescription(error.isEmpty() ? reply.error().message() : error);
                    else
                    {
                        m_name->setValue(tr("Now: %1").arg(reply.value().variant().toString()));
                        m_name->setDescription(error.isEmpty() ? tr("Device name") : error);
                    }
                });
        };
        setHostname(QStringLiteral("SetStaticHostname"), name,
            [this, name, complete](const QString &error)
            {
                if (!error.isEmpty())
                {
                    complete(error);
                    return;
                }
                setHostname(QStringLiteral("SetHostname"), name, complete);
            });
    }
    void setHostname(
        const QString &method, const QString &name, const std::function<void(const QString &)> &done)
    {
        const QString service = QStringLiteral("org.freedesktop.hostname1");
        auto call = QDBusMessage::createMethodCall(
            service, QStringLiteral("/org/freedesktop/hostname1"), service, method);
        call.setInteractiveAuthorizationAllowed(true);
        call << name << true;
        auto *watcher
            = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 120000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [watcher, done]
            {
                const QDBusPendingReply<> reply = *watcher;
                watcher->deleteLater();
                done(reply.isError() ? reply.error().message() : QString());
            });
    }
    QPushButton *m_rename;

    SettingRow *m_name;
    FactsCard *m_device;
    FactsCard *m_system;
    QList<QPair<QString, QString>> m_facts;
};

} // namespace

void countUpdates(QObject *context, const std::function<void(int)> &done)
{
    simulateUpgrade(
        context, [done](const QList<Upgrade> &upgrades, bool ok) { done(ok ? int(upgrades.size()) : -1); });
}

SettingsPage *createUpdatesPage(QWidget *parent) { return new UpdatesPage(parent); }
SettingsPage *createAboutPage(QWidget *parent) { return new AboutPage(parent); }

#include "about.moc"
