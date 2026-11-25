// SPDX-License-Identifier: GPL-3.0-or-later
// Network settings using bounded nmcli queries and the shared activation helper.
#include "networkdetails.h"
#include "pages.h"
#include "widgets.h"
#include "wifi.h"
#include "physical_client.h"
#include "physical_panel.h"
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
namespace
{
using NetworkDetails::splitTerse;

class NetworkPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit NetworkPage(QWidget *parent)
        : SettingsPage(tr("Network & internet"), parent)
    {
        addKeywords({ QStringLiteral("wifi"), QStringLiteral("Wi-Fi"), QStringLiteral("ethernet"),
            QStringLiteral("IP"), QStringLiteral("DNS"), tr("Connections") });
        if (Karyo::Ui::physicalNetworkManaged()) {
            column()->addWidget(sectionHeading(tr("Wi-Fi and Ethernet")));
            m_physical = new Karyo::Ui::PhysicalNetworkPanel(this);
            column()->addWidget(m_physical);
            column()->addWidget(sectionHeading(tr("Desktop connections")));
        }
        m_status = new FactsCard(this);
        column()->addWidget(m_status);
        m_wifiStatus = new QLabel(this);
        m_wifiStatus->setObjectName("networkStatus");
        m_wifiStatus->setWordWrap(true);
        m_wifiStatus->setTextFormat(Qt::PlainText);
        m_wifiStatus->hide();
        column()->addWidget(m_wifiStatus);
        m_wifi = new SettingRow(QStringLiteral("wifi"), tr("Wi-Fi"), QString(), this);
        m_wifiToggle = m_wifi->addToggle(false);
        m_wifiToggle->setObjectName("wifiPower");
        m_wifiToggle->setEnabled(false);
        connect(m_wifiToggle, &QAbstractButton::toggled, this,
            [this](bool on)
            {
                if (m_connecting)
                    return;
                beginChange(tr("Changing Wi-Fi state…"));
                runCommand(
                    this, QStringLiteral("nmcli"),
                    { QStringLiteral("radio"), QStringLiteral("wifi"),
                        on ? QStringLiteral("on") : QStringLiteral("off") },
                    [this](const QString &) { finishChange({ }); },
                    [this](const QString &error) { finishChange(error); });
            });
        m_wifi->hide();
        column()->addWidget(m_wifi);
        m_networksHeading = sectionHeading(tr("Wi-Fi networks"));
        m_networksHeading->hide();
        column()->addWidget(m_networksHeading);
        m_networkControls = new QWidget(this);
        m_networks = new QVBoxLayout(m_networkControls);
        m_networks->setContentsMargins(0, 0, 0, 0);
        m_networks->setSpacing(10);
        column()->addWidget(m_networkControls);
        column()->addWidget(sectionHeading(tr("Saved connections")));
        m_connectionControls = new QWidget(this);
        m_connections = new QVBoxLayout(m_connectionControls);
        m_connections->setContentsMargins(0, 0, 0, 0);
        m_connections->setSpacing(10);
        column()->addWidget(m_connectionControls);
        QTimer::singleShot(0, this, [this] { refresh(); });
    }

    void refresh() override
    {
        if (m_connecting)
            return;
        ++m_generation;
        if (m_physical) m_physical->refresh();
        readStatus();
        readConnections();
    }

private:
    void query(const QStringList &args, const std::function<void(const QString &)> &done,
        const std::function<void(const QString &)> &failed = { })
    {
        const auto generation = m_generation;
        runCommand(
            this, QStringLiteral("nmcli"), args,
            [this, generation, done](const QString &text)
            {
                if (generation == m_generation && !m_connecting)
                    done(text);
            },
            [this, generation, failed](const QString &error)
            {
                if (generation != m_generation || m_connecting)
                    return;
                if (failed)
                {
                    failed(error);
                    return;
                }
                m_wifiStatus->setText(error);
                m_wifiStatus->show();
            });
    }
    void beginChange(const QString &message)
    {
        m_connecting = true;
        ++m_generation;
        m_wifiToggle->setEnabled(false);
        m_networkControls->setEnabled(false);
        m_connectionControls->setEnabled(false);
        m_wifiStatus->setText(message);
        m_wifiStatus->show();
    }
    void finishChange(const QString &error)
    {
        m_connecting = false;
        m_networkControls->setEnabled(true);
        m_connectionControls->setEnabled(true);
        m_wifiStatus->setText(error.isEmpty() ? tr("Connection updated") : error);
        m_wifiStatus->show();
        refresh();
    }
    void readWifiPresence()
    {
        query(
            { QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("TYPE"), QStringLiteral("device") },
            [this](const QString &out)
            {
                const bool wifi = out.split(QLatin1Char('\n')).contains(QStringLiteral("wifi"));
                m_wifi->setVisible(wifi);
                m_networksHeading->setVisible(wifi);
                clear(m_networks);
                if (wifi)
                    readWifi();
                else
                    m_wifiToggle->setEnabled(false);
            });
    }

    void showStatus(const NetworkDetails::Connection &info)
    {
        if (!info.valid())
        {
            m_status->setFacts({ { tr("Status"), tr("Not connected") } });
            return;
        }
        QList<QPair<QString, QString>> facts { { tr("Connection"), info.name },
            { tr("Device"), info.device + QStringLiteral(" · ") + info.type },
            { tr("IPv4 address"), info.ipv4 }, { tr("Gateway"), info.gateway },
            { tr("DNS servers"), info.dns }, { tr("IPv6 address"), info.ipv6 },
            { tr("Hardware address"), info.mac } };
        if (info.external)
            facts.append({ tr("Network management"), tr("Managed outside NetworkManager") });
        m_status->setFacts(facts);
    }
    void readStatus()
    {
        query(
            { QStringLiteral("-t"), QStringLiteral("-f"),
                QStringLiteral("GENERAL.DEVICE,GENERAL.TYPE,GENERAL.STATE,GENERAL.CONNECTION,GENERAL.HWADDR,"
                               "IP4.ADDRESS,IP4.GATEWAY,IP4.DNS,IP6.ADDRESS"),
                QStringLiteral("device"), QStringLiteral("show") },
            [this](const QString &out)
            {
                const auto system = NetworkDetails::systemConnection();
                auto info = NetworkDetails::fromNmcli(out, system.device);
                showStatus(info.valid() ? info : system);
            },
            [this](const QString &error)
            {
                showStatus(NetworkDetails::systemConnection());
                m_wifiStatus->setText(error);
                m_wifiStatus->show();
            });
    }

    void readConnections()
    {
        query({ QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,UUID,TYPE,DEVICE"),
                  QStringLiteral("connection"), QStringLiteral("show") },
            [this](const QString &out)
            {
                clear(m_connections);
                m_savedWifi.clear();
                for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                {
                    const QStringList f = splitTerse(line);
                    if (f.size() < 4 || f.at(2) == QLatin1String("loopback"))
                        continue;
                    if (f.at(2) == QLatin1String("802-11-wireless") || f.at(2) == QLatin1String("wifi"))
                        m_savedWifi.insert(f.at(0), f.at(1));
                    const bool active = !f.at(3).isEmpty() && f.at(3) != QLatin1String("--");
                    const QString glyph = f.at(2).contains(QLatin1String("wireless")) ? QStringLiteral("wifi")
                        : f.at(2).contains(QLatin1String("vpn")) || f.at(2) == QLatin1String("wireguard")
                        ? QStringLiteral("shield")
                        : QStringLiteral("network");
                    auto *row = new SettingRow(
                        glyph, f.at(0), active ? tr("Connected · %1").arg(f.at(3)) : f.at(2), this);
                    auto *button = new QPushButton(active ? tr("Disconnect") : tr("Connect"));
                    const QString uuid = f.at(1);
                    connect(button, &QPushButton::clicked, this,
                        [this, uuid, active]
                        {
                            if (m_connecting)
                                return;
                            beginChange(active ? tr("Disconnecting…") : tr("Connecting…"));
                            runCommand(
                                this, QStringLiteral("nmcli"),
                                { QStringLiteral("--wait"), QStringLiteral("45"),
                                    QStringLiteral("connection"),
                                    active ? QStringLiteral("down") : QStringLiteral("up"),
                                    QStringLiteral("uuid"), uuid },
                                [this](const QString &) { finishChange({ }); },
                                [this](const QString &error) { finishChange(error); }, 55000);
                        });
                    row->setControl(button);
                    m_connections->addWidget(row);
                }
                readWifiPresence();
            });
    }

    void readWifi()
    {
        query({ QStringLiteral("radio"), QStringLiteral("wifi") },
            [this](const QString &out)
            {
                const QString state = out.trimmed();
                if (state != QLatin1String("enabled") && state != QLatin1String("disabled"))
                {
                    m_wifiToggle->setEnabled(false);
                    m_wifiStatus->setText(tr("NetworkManager returned an invalid Wi-Fi state."));
                    m_wifiStatus->show();
                    return;
                }
                const QSignalBlocker block(m_wifiToggle);
                m_wifiToggle->setChecked(state == QLatin1String("enabled"));
                m_wifiToggle->setEnabled(true);
                m_wifi->setValue(m_wifiToggle->isChecked() ? tr("On") : tr("Off"));
            });
        query({ QStringLiteral("-t"), QStringLiteral("-f"),
                  QStringLiteral("IN-USE,SSID,SIGNAL,SECURITY,DEVICE,DBUS-PATH"), QStringLiteral("device"),
                  QStringLiteral("wifi"), QStringLiteral("list") },
            [this](const QString &out)
            {
                clear(m_networks);
                QStringList seen;
                for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts))
                {
                    const QStringList f = splitTerse(line);
                    if (f.size() < 6 || f.at(1).isEmpty() || seen.contains(f.at(1)))
                        continue;
                    seen.append(f.at(1));
                    const bool inUse = f.at(0) == QLatin1String("*");
                    const bool secured = !f.at(3).isEmpty() && f.at(3) != QLatin1String("--");
                    auto *row = new SettingRow(QStringLiteral("wifi"), f.at(1),
                        tr("Signal %1%").arg(f.at(2)) + (secured ? tr(" · Secured") : QString()), this);
                    if (!inUse)
                    {
                        auto *button = new QPushButton(tr("Connect"));
                        connect(button, &QPushButton::clicked, this, [this, f] { connectWifi(f); });
                        row->setControl(button);
                    }
                    else
                    {
                        row->setValue(tr("Connected"));
                    }
                    m_networks->addWidget(row);
                }
            });
    }

    void connectWifi(const QStringList &point)
    {
        namespace Ui = Karyo::Ui;
        if (m_connecting)
            return;
        const QString uuid = m_savedWifi.value(point.at(1));
        if (!uuid.isEmpty())
        {
            beginChange(tr("Connecting…"));
            runCommand(
                this, QStringLiteral("nmcli"),
                { QStringLiteral("--wait"), QStringLiteral("45"), QStringLiteral("connection"),
                    QStringLiteral("up"), QStringLiteral("uuid"), uuid },
                [this](const QString &) { finishChange({ }); },
                [this](const QString &error) { finishChange(error); }, 55000);
            return;
        }
        const auto security = Ui::wifiSecurity(point.at(3));
        if (security == Ui::WifiSecurity::Unsupported)
        {
            m_wifiStatus->setText(
                tr("This network requires additional authentication. Use the connection editor."));
            m_wifiStatus->show();
            return;
        }
        QString password;
        if (security != Ui::WifiSecurity::Open)
        {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("Connect to %1").arg(point.at(1)));
            dialog.setMinimumWidth(420);
            auto *form = new QVBoxLayout(&dialog);
            auto *label = new QLabel(tr("Network password"), &dialog);
            label->setTextFormat(Qt::PlainText);
            auto *field = new QLineEdit(&dialog);
            label->setBuddy(field);
            field->setEchoMode(QLineEdit::Password);
            field->setMinimumHeight(42);
            form->addWidget(label);
            form->addWidget(field);
            auto *hint = new QLabel(Ui::wifiPasswordHint(security), &dialog);
            hint->setWordWrap(true);
            form->addWidget(hint);
            auto *show = new QCheckBox(tr("Show password"), &dialog);
            form->addWidget(show);
            connect(show, &QCheckBox::toggled, field, [field](bool visible)
                { field->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password); });
            auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            auto *submit = buttons->button(QDialogButtonBox::Ok);
            submit->setText(tr("Connect"));
            submit->setEnabled(false);
            connect(field, &QLineEdit::textChanged, submit, [submit, security](const QString &value)
                { submit->setEnabled(Ui::validWifiPassword(security, value)); });
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            form->addWidget(buttons);
            field->setFocus();
            if (dialog.exec() != QDialog::Accepted)
                return;
            password = field->text();
        }
        beginChange(tr("Connecting…"));
        Ui::connectWifi(this, point.at(4), point.at(5), security, password,
            [this](const QString &error) { finishChange(error); });
    }

    static void clear(QVBoxLayout *layout)
    {
        while (QLayoutItem *item = layout->takeAt(0))
        {
            delete item->widget();
            delete item;
        }
    }

    quint64 m_generation = 0;
    Karyo::Ui::PhysicalNetworkPanel *m_physical = nullptr;
    QWidget *m_networkControls, *m_connectionControls;
    FactsCard *m_status;
    SettingRow *m_wifi;
    ToggleSwitch *m_wifiToggle;
    QLabel *m_networksHeading;
    QVBoxLayout *m_networks;
    QVBoxLayout *m_connections;
    QMap<QString, QString> m_savedWifi;
    QLabel *m_wifiStatus = nullptr;
    bool m_connecting = false;
};

} // namespace

SettingsPage *createNetworkPage(QWidget *parent) { return new NetworkPage(parent); }
#include "network.moc"
