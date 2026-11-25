// SPDX-License-Identifier: GPL-3.0-or-later
#include "physical_panel.h"
#include "physical_client.h"
#include "wifi.h"
#include <QCheckBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSet>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace Karyo::Ui {
PhysicalNetworkPanel::PhysicalNetworkPanel(QWidget *parent) : QWidget(parent)
{
    auto *column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(10);
    auto *heading = new QHBoxLayout;
    auto *refreshButton = new QPushButton(tr("Refresh"), this);
    connect(refreshButton, &QPushButton::clicked, this, &PhysicalNetworkPanel::refresh);
    heading->addWidget(refreshButton);
    heading->addStretch();
    m_radio = new QPushButton(this);
    m_radio->setObjectName(QStringLiteral("physicalRadio"));
    connect(m_radio, &QPushButton::clicked, this, [this] {
        change({{QStringLiteral("operation"), QStringLiteral("radio")}, {QStringLiteral("enabled"), !m_wirelessEnabled}});
    });
    heading->addWidget(m_radio);
    column->addLayout(heading);
    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("physicalConnections"));
    m_list->setAccessibleName(tr("Wi-Fi and Ethernet connections"));
    m_list->setFixedHeight(190);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::currentItemChanged, this, [this] {
        m_form->hide();m_password->clear();status({});updateAction();
    });
    connect(m_list, &QListWidget::itemActivated, this, [this] { activate(); });
    column->addWidget(m_list);
    m_status = new QLabel(this);
    m_status->setObjectName(QStringLiteral("physicalStatus"));
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    m_status->hide();
    column->addWidget(m_status);
    m_form = new QWidget(this);
    auto *form = new QVBoxLayout(m_form);
    form->setContentsMargins(0, 0, 0, 0);
    auto *label = new QLabel(tr("Network password"), m_form);
    m_password = new QLineEdit(m_form);
    m_password->setObjectName(QStringLiteral("physicalPassword"));
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setMinimumHeight(42);
    label->setBuddy(m_password);
    form->addWidget(label);form->addWidget(m_password);
    m_hint = new QLabel(m_form);m_hint->setWordWrap(true);form->addWidget(m_hint);
    auto *show = new QCheckBox(tr("Show password"), m_form);
    connect(show, &QCheckBox::toggled, this, [this](bool visible) {
        m_password->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
    });
    form->addWidget(show);
    connect(m_password, &QLineEdit::textChanged, this, [this] { updateAction(); });
    connect(m_password, &QLineEdit::returnPressed, this, [this] { activate(); });
    m_form->hide();column->addWidget(m_form);
    auto *footer = new QHBoxLayout;footer->addStretch();
    m_action = new QPushButton(tr("Connect"), this);
    m_action->setObjectName(QStringLiteral("physicalConnect"));
    m_action->setEnabled(false);
    connect(m_action, &QPushButton::clicked, this, [this] { activate(); });
    footer->addWidget(m_action);column->addLayout(footer);
}

void PhysicalNetworkPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

QJsonObject PhysicalNetworkPanel::selected() const
{
    return m_list->currentItem() ? QJsonObject::fromVariantMap(m_list->currentItem()->data(Qt::UserRole).toMap()) : QJsonObject();
}

void PhysicalNetworkPanel::refresh()
{
    if (m_loading || m_busy)
        return;
    m_loading = true;
    readPhysicalNetwork(this, [this](const QJsonObject &state, const QString &error) {
        m_loading = false;
        if (!error.isEmpty()) { status(error);return; }
        m_owner = state.value(QStringLiteral("owner")).toString();
        const auto old = selected();
        const QString key = old.value(QStringLiteral("uuid")).toString() + old.value(QStringLiteral("path")).toString();
        QSignalBlocker blocker(m_list);
        m_list->clear();
        m_wirelessEnabled = state.value(QStringLiteral("wireless_enabled")).toBool();
        m_radio->setVisible(state.value(QStringLiteral("wireless")).toBool());
        m_radio->setText(m_wirelessEnabled ? tr("Wi-Fi on") : tr("Wi-Fi off"));
        QSet<QString> names;
        const auto add = [this, &key](QJsonObject value, const QString &name) {
            auto *item = new QListWidgetItem(name, m_list);
            item->setData(Qt::UserRole, value.toVariantMap());
            item->setSizeHint(QSize(0, 34));
            if (value.value(QStringLiteral("uuid")).toString() + value.value(QStringLiteral("path")).toString() == key)
                m_list->setCurrentItem(item);
        };
        for (const auto &row : state.value(QStringLiteral("connections")).toArray()) {
            auto value = row.toObject();
            const QString name = value.value(QStringLiteral("name")).toString();
            names.insert(name);
            add(value, value.value(QStringLiteral("active")).toBool() ? tr("%1 · Connected").arg(name) : name);
        }
        if (m_wirelessEnabled) {
            for (const auto &row : state.value(QStringLiteral("access_points")).toArray()) {
                auto value = row.toObject();
                const QString name = value.value(QStringLiteral("ssid")).toString();
                if (names.contains(name))
                    continue;
                names.insert(name);
                add(value, wifiSecurity(value.value(QStringLiteral("security")).toString()) == WifiSecurity::Open
                        ? name : tr("%1 · Secured").arg(name));
            }
        }
        if (!m_list->currentItem() && m_list->count())
            m_list->setCurrentRow(0);
        if (selected().isEmpty() || key != selected().value(QStringLiteral("uuid")).toString() + selected().value(QStringLiteral("path")).toString()) {
            m_form->hide();m_password->clear();
        }
        updateAction();Q_EMIT geometryChanged();
    });
}

void PhysicalNetworkPanel::status(const QString &message)
{
    m_status->setText(message);m_status->setVisible(!message.isEmpty());
    Q_EMIT geometryChanged();
}

void PhysicalNetworkPanel::updateAction()
{
    const auto item = selected();
    const bool external = item.value(QStringLiteral("external")).toBool();
    const bool wifi = item.contains(QStringLiteral("path"));
    const auto security = wifiSecurity(item.value(QStringLiteral("security")).toString());
    m_action->setText(item.value(QStringLiteral("active")).toBool() ? tr("Disconnect") : tr("Connect"));
    m_action->setEnabled(!item.isEmpty() && !external && !m_busy && (!wifi || m_form->isHidden()
        || validWifiPassword(security, m_password->text())));
    m_radio->setEnabled(!m_busy);
    if (external && !m_busy)
        status(tr("Managed outside NetworkManager."));
}

void PhysicalNetworkPanel::activate()
{
    if (!m_action->isEnabled())
        return;
    const auto item = selected();
    if (!item.contains(QStringLiteral("path"))) {
        change({{QStringLiteral("operation"), item.value(QStringLiteral("active")).toBool()
                    ? QStringLiteral("disconnect") : QStringLiteral("activate")},
                {QStringLiteral("uuid"), item.value(QStringLiteral("uuid"))}});
        return;
    }
    const auto security = wifiSecurity(item.value(QStringLiteral("security")).toString());
    if (security == WifiSecurity::Unsupported) { status(tr("This network needs additional administrator setup."));return; }
    if (security != WifiSecurity::Open && m_form->isHidden()) {
        m_hint->setText(wifiPasswordHint(security));m_form->show();m_password->setFocus();updateAction();
        Q_EMIT geometryChanged();return;
    }
    change({{QStringLiteral("operation"), QStringLiteral("wifi")},
            {QStringLiteral("interface"), item.value(QStringLiteral("interface"))},
            {QStringLiteral("access_point"), item.value(QStringLiteral("path"))},
            {QStringLiteral("password"), m_password->text()}});
}

void PhysicalNetworkPanel::change(QJsonObject request)
{
    if (m_busy)
        return;
    m_busy = true;m_list->setEnabled(false);m_form->setEnabled(false);updateAction();
    status(tr("Updating connection…"));
    request.insert(QStringLiteral("owner"), m_owner);
    changePhysicalNetwork(this, std::move(request), [this](const QString &error) {
        m_busy = false;m_list->setEnabled(true);m_form->setEnabled(true);
        if (error.isEmpty()) { m_form->hide();m_password->clear(); }
        status(error.isEmpty() ? tr("Connection updated") : error);
        updateAction();refresh();
    });
}
}
