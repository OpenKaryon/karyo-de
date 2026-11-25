// SPDX-License-Identifier: GPL-3.0-or-later
#include "integrations.h"
#include "quicksettings.h"

#include "networkstatus.h"
#include "notify/notificationserver.h"
#include "systemcontrols.h"

#include <karyo/ui/desktopentry.h>
#include <karyo/ui/icons.h>
#include <karyo/ui/paint.h>
#include <karyo/ui/theme.h>
#include <karyo/ui/window.h>

#include <QAbstractButton>
#include <QCoreApplication>
#include "wifi.h"
#include "physical_client.h"
#include "physical_panel.h"
#include <karyo/ui/menu.h>
#include <QCheckBox>
#include <QLineEdit>
#include <QMenu>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSet>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>

#include <functional>

namespace Ui = Karyo::Ui;

namespace {

constexpr int CardWidth = 420;
constexpr int CardRadius = 8;
constexpr int EdgeGap = 12;
constexpr int TaskbarHeight = 48;
constexpr int Padding = 22;

// A 3x2 grid tile: glyph, title and state; filled with the accent when on.
// Tiles with more behind them show a chevron, which runs "more".
class QuickTile : public QAbstractButton
{
public:
    QuickTile(const QString &glyph, const QString &title, const QString &state, bool on,
              std::function<void()> toggle, std::function<void()> more, QWidget *parent)
        : QAbstractButton(parent)
        , m_glyph(glyph)
        , m_state(state)
        , m_on(on)
        , m_toggle(std::move(toggle))
        , m_more(std::move(more))
    {
        setText(title);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(86);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setEnabled(m_toggle || m_more);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Ui::Theme &t = Ui::Theme::current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool hover = underMouse() && isEnabled();
        QColor fill = m_on ? t.accent : t.card;
        if (hover)
            fill = m_on ? Ui::accent(600) : Ui::mix(t.text, t.card, 6);
        if (hasFocus() && isEnabled())
            fill = Ui::mix(t.text, fill, 12);
        if (isDown())
            fill = m_on ? Ui::accent(700) : Ui::mix(t.text, t.card, 13);
        Ui::fillRounded(painter, rect(), 8, fill);
        const QColor ink = m_on ? Ui::neutral(100) : t.text;
        const qreal dpr = devicePixelRatioF();
        painter.drawPixmap(14, 14, Ui::icon(m_glyph, 18, ink, dpr));
        if (m_more)
            painter.drawPixmap(width() - 14 - 16, 15, Ui::icon(QStringLiteral("chevron-right"), 16, ink, dpr));
        painter.setFont(Ui::bodyFont(13, QFont::Bold));
        painter.setPen(ink);
        const QFontMetrics title(painter.font());
        painter.drawText(QRect(14, 44, width() - 24, 18), Qt::AlignVCenter | Qt::AlignLeft,
                         title.elidedText(text(), Qt::ElideRight, width() - 24));
        painter.setFont(Ui::bodyFont(12));
        painter.setPen(m_on ? Ui::alpha(Ui::neutral(100), 85) : t.muted);
        painter.drawText(QRect(14, 62, width() - 24, 16), Qt::AlignVCenter | Qt::AlignLeft,
                         QFontMetrics(painter.font()).elidedText(m_state, Qt::ElideRight, width() - 24));
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        const bool inside = rect().contains(event->position().toPoint());
        const bool onChevron = m_more && event->position().x() > width() - 40 && event->position().y() < 40;
        QAbstractButton::mouseReleaseEvent(event);
        if (!inside || event->button() != Qt::LeftButton)
            return;
        if (onChevron || !m_toggle) {
            if (m_more)
                m_more();
        } else {
            m_toggle();
        }
    }

private:
    QString m_glyph;
    QString m_state;
    bool m_on;
    std::function<void()> m_toggle;
    std::function<void()> m_more;
};

class NetworkPage : public QWidget
{
    Q_DECLARE_TR_FUNCTIONS(NetworkPage)

public:
    NetworkPage(NetworkStatus *network, const std::function<void()> &back,
                const std::function<void()> &settings, const std::function<void()> &resize, QWidget *parent)
        : QWidget(parent)
        , m_network(network)
        , m_settings(settings)
        , m_resize(resize)
        , m_wifi(new QPushButton(this))
        , m_list(new QListWidget(this))
        , m_action(new QPushButton(this))
        , m_status(new QLabel(this))
    {
        auto *column = new QVBoxLayout(this);
        column->setContentsMargins(Padding, Padding, Padding, 16);
        column->setSpacing(10);

        auto *heading = new QHBoxLayout;
        auto *backButton = new Ui::IconButton(QStringLiteral("chevron-left"), this, {32, 32}, 16);
        backButton->setHint(tr("Quick settings"));
        connect(backButton, &QAbstractButton::clicked, this, back);
        heading->addWidget(backButton);
        auto *title = new QLabel(tr("Network"), this);
        title->setFont(Ui::bodyFont(16, QFont::DemiBold));
        heading->addWidget(title, 1);
        connect(m_wifi, &QPushButton::clicked, this, [this] {
            run({QStringLiteral("radio"), QStringLiteral("wifi"),
                 m_network->isWirelessEnabled() ? QStringLiteral("off") : QStringLiteral("on")},
                [this](const QString &result, bool ok) {
                    if (!ok)
                        setStatus(result.isEmpty() ? tr("Could not change the Wi-Fi radio.") : result);
                    refresh();
                });
        });
        heading->addWidget(m_wifi);
        column->addLayout(heading);
        if (Ui::physicalNetworkManaged()) {
            m_wifi->hide();m_list->hide();m_action->hide();m_status->hide();
            m_physicalPanel = new Ui::PhysicalNetworkPanel(this);
            column->addWidget(m_physicalPanel);
            connect(m_physicalPanel, &Ui::PhysicalNetworkPanel::geometryChanged, this, m_resize);
            connect(m_network, &NetworkStatus::changed, this, [this] {
                if (isVisible()) m_physicalPanel->refresh();
            });
            return;
        }

        m_list->setAccessibleName(tr("Network connections"));
        m_list->setFixedHeight(190);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        connect(m_list, &QListWidget::currentItemChanged, this, [this] {
            if (!m_credentials || m_rendering)
                return;
            m_credentials->hide();
            m_password->clear();
            setStatus({});
            updateAction();
        });
        connect(m_list, &QListWidget::itemActivated, this, [this] { activate(); });
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_list, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
            auto *menu = new QMenu(m_list);
            Ui::styleMenu(menu);
            connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
            menu->addAction(tr("Network settings"), this, m_settings);
            menu->popup(m_list->viewport()->mapToGlobal(pos));
        });
        column->addWidget(m_list);
        m_status->setWordWrap(true);
        m_status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_status->setTextFormat(Qt::PlainText);
        m_status->hide();
        column->addWidget(m_status);

        m_credentials = new QWidget(this);
        auto *form = new QVBoxLayout(m_credentials);
        form->setContentsMargins(0, 4, 0, 4);
        auto *label = new QLabel(tr("Network password"), m_credentials);
        m_password = new QLineEdit(m_credentials);
        m_password->setEchoMode(QLineEdit::Password);
        m_password->setMinimumHeight(42);
        label->setBuddy(m_password);
        form->addWidget(label);
        form->addWidget(m_password);
        m_hint = new QLabel(m_credentials);
        m_hint->setWordWrap(true);
        form->addWidget(m_hint);
        auto *show = new QCheckBox(tr("Show password"), m_credentials);
        connect(show, &QCheckBox::toggled, m_password, [this](bool visible) {
            m_password->setEchoMode(visible ? QLineEdit::Normal : QLineEdit::Password);
        });
        form->addWidget(show);
        m_credentials->hide();
        column->addWidget(m_credentials);
        connect(m_password, &QLineEdit::textChanged, this, [this] { updateAction(); });
        connect(m_password, &QLineEdit::returnPressed, this, [this] { activate(); });

        auto *footer = new QHBoxLayout;
        footer->addStretch();
        connect(m_action, &QPushButton::clicked, this, [this] { activate(); });
        footer->addWidget(m_action);
        column->addLayout(footer);

        connect(m_network, &NetworkStatus::changed, this, [this] {
            if (isVisible())
                refresh();
        });
        updateAction();
    }

    void refresh()
    {
        if (m_physicalPanel) { m_physicalPanel->refresh();return; }
        m_wifi->setVisible(m_network->hasWireless());
        m_wifi->setText(m_network->isWirelessEnabled() ? tr("Wi-Fi on") : tr("Wi-Fi off"));
        if (m_refreshing || m_actionPending)
            return;
        m_refreshing = true;
        run({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("NAME,UUID,TYPE,DEVICE"),
             QStringLiteral("connection"), QStringLiteral("show")}, [this](const QString &out, bool ok) {
            m_connections = ok ? out : QString();
            if (!ok && m_status->isHidden())
                setStatus(out);
            if (!ok || !m_network->hasWireless() || !m_network->isWirelessEnabled()) {
                render({});
                return;
            }
            run({QStringLiteral("-t"), QStringLiteral("-f"), QStringLiteral("IN-USE,SSID,SECURITY,DEVICE,DBUS-PATH"),
                 QStringLiteral("device"), QStringLiteral("wifi"), QStringLiteral("list")},
                [this](const QString &points, bool ok) {
                    if (!ok && m_status->isHidden())
                        setStatus(points);
                    render(ok ? points : QString());
                });
        });
    }

private:
    enum Kind { Saved, Active, Wifi };
    using Result = std::function<void(const QString &, bool)>;

    void run(const QStringList &args, const Result &done, int timeout = 10)
    {
        // Keep an activation running if the user dismisses the flyout.
        auto *process = new QProcess(QCoreApplication::instance());
        QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
        environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
        process->setProcessEnvironment(environment);
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        connect(process, &QProcess::finished, this, [process, done](int code, QProcess::ExitStatus status) {
            const bool ok = status == QProcess::NormalExit && code == 0;
            done(QString::fromUtf8(ok ? process->readAllStandardOutput() : process->readAllStandardError()).trimmed(), ok);
        });
        connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                process->deleteLater();
        });
        connect(process, &QProcess::errorOccurred, this, [done](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                done(tr("Could not start nmcli."), false);
        });
        process->start(QStringLiteral("nmcli"), QStringList{QStringLiteral("--wait"), QString::number(timeout)} + args);
    }

    static QStringList fields(const QString &line)
    {
        QStringList result{QString()};
        bool escaped = false;
        for (QChar character : line) {
            if (!escaped && character == QLatin1Char(':'))
                result.append(QString());
            else if (escaped || character != QLatin1Char('\\'))
                result.last().append(character);
            escaped = !escaped && character == QLatin1Char('\\');
        }
        return result;
    }

    void render(const QString &points)
    {
        m_refreshing = false;
        const int selectedKind = m_list->currentItem() ? m_list->currentItem()->data(Qt::UserRole + 1).toInt() : -1;
        const QString selected = m_list->currentItem()
                                     ? m_list->currentItem()->data(Qt::UserRole).toString() : QString();
        m_rendering = true;
        m_list->clear();
        QSet<QString> seen;
        const auto add = [this, &selected, selectedKind](const QString &name, const QString &key, Kind kind, const QStringList &point = {}) {
            auto *item = new QListWidgetItem(name, m_list);
            item->setData(Qt::UserRole, key);
            item->setData(Qt::UserRole + 1, int(kind));
            item->setData(Qt::UserRole + 2, point);
            item->setSizeHint(QSize(0, 34));
            if (key == selected && (kind == Wifi) == (selectedKind == Wifi))
                m_list->setCurrentItem(item);
        };
        for (const QString &line : m_connections.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList entry = fields(line);
            if (entry.size() != 4 || entry.at(1).isEmpty())
                continue;
            const QString &type = entry.at(2);
            if (type != QLatin1String("802-3-ethernet") && type != QLatin1String("ethernet")
                && type != QLatin1String("802-11-wireless") && type != QLatin1String("wifi")
                && type != QLatin1String("vpn") && type != QLatin1String("wireguard"))
                continue;
            if (type == QLatin1String("802-11-wireless") || type == QLatin1String("wifi"))
                seen.insert(entry.at(0));
            const bool active = !entry.at(3).isEmpty() && entry.at(3) != QLatin1String("--");
            add(active ? tr("%1 · Connected").arg(entry.at(0)) : entry.at(0), entry.at(1), active ? Active : Saved);
        }
        for (const QString &line : points.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QStringList entry = fields(line);
            if (entry.size() != 5 || entry.at(1).isEmpty() || seen.contains(entry.at(1)))
                continue;
            seen.insert(entry.at(1));
            if (entry.at(0) == QLatin1String("*"))
                continue;
            const bool secured = Ui::wifiSecurity(entry.at(2)) != Ui::WifiSecurity::Open;
            add(secured ? tr("%1 · Secured").arg(entry.at(1)) : entry.at(1), entry.at(1), Wifi, entry);
        }
        m_rendering = false;
        if (!m_list->currentItem()) {
            m_credentials->hide();
            m_password->clear();
        }
        updateAction();
        m_resize();
    }

    void setStatus(const QString &message)
    {
        m_status->setText(message);
        m_status->setVisible(!message.isEmpty());
        m_resize();
    }

    void updateAction()
    {
        const auto *item = m_list->currentItem();
        const int kind = item ? item->data(Qt::UserRole + 1).toInt() : Saved;
        const auto security = Ui::wifiSecurity(item ? item->data(Qt::UserRole + 2).toStringList().value(2) : QString());
        m_action->setText(kind == Active ? tr("Disconnect") : tr("Connect"));
        m_action->setEnabled(item && !m_actionPending && (m_credentials->isHidden()
                             || Ui::validWifiPassword(security, m_password->text())));
    }

    void activate()
    {
        const auto *item = m_list->currentItem();
        if (!item || !m_action->isEnabled())
            return;
        const int kind = item->data(Qt::UserRole + 1).toInt();
        const QString key = item->data(Qt::UserRole).toString();
        const QStringList point = item->data(Qt::UserRole + 2).toStringList();
        const auto security = Ui::wifiSecurity(point.value(2));
        if (security == Ui::WifiSecurity::Unsupported) {
            setStatus(tr("This network requires additional authentication. Open Network settings from the context menu."));
            return;
        }
        if (security != Ui::WifiSecurity::Open && m_credentials->isHidden()) {
            m_hint->setText(Ui::wifiPasswordHint(security));
            m_credentials->show();
            m_password->setFocus();
            setStatus({});
            updateAction();
            return;
        }
        m_actionPending = true;
        m_list->setEnabled(false);
        m_credentials->setEnabled(false);
        setStatus(kind == Active ? tr("Disconnecting…") : tr("Connecting…"));
        updateAction();
        const auto finish = [this, kind](const QString &error) {
            m_actionPending = false;
            m_list->setEnabled(true);
            m_credentials->setEnabled(true);
            if (error.isEmpty()) {
                m_credentials->hide();
                m_password->clear();
                setStatus(kind == Active ? tr("Disconnected.") : tr("Connected."));
            } else {
                setStatus(error);
                if (!m_credentials->isHidden()) {
                    m_password->setFocus();
                    m_password->selectAll();
                }
            }
            updateAction();
            refresh();
        };
        if (kind == Wifi) {
            Ui::connectWifi(this, point.at(3), point.at(4), security, m_password->text(), finish);
        } else {
            run({QStringLiteral("connection"), kind == Active ? QStringLiteral("down") : QStringLiteral("up"),
                 QStringLiteral("uuid"), key}, [this, finish](const QString &result, bool ok) {
                     finish(ok ? QString() : result.isEmpty() ? tr("Could not change the connection.") : result);
                 }, 45);
        }
    }

    NetworkStatus *m_network;
    std::function<void()> m_settings;
    std::function<void()> m_resize;
    Ui::PhysicalNetworkPanel *m_physicalPanel = nullptr;
    QWidget *m_credentials = nullptr;
    QLineEdit *m_password = nullptr;
    QLabel *m_hint = nullptr;
    bool m_rendering = false;
    QPushButton *m_wifi;
    QListWidget *m_list;
    QPushButton *m_action;
    QLabel *m_status;
    QString m_connections;
    bool m_refreshing = false;
    bool m_actionPending = false;
};


void launchFirst(const QStringList &ids)
{
    for (const QString &id : ids) {
        if (const auto entry = DesktopEntry::find(id)) {
            entry->launch();
            return;
        }
    }
}

QSlider *slider(QWidget *parent)
{
    auto *s = new QSlider(Qt::Horizontal, parent);
    s->setRange(0, 100);
    s->setMinimumHeight(32);
    return s;
}

} // namespace

QuickSettings::QuickSettings(NetworkStatus *network, NotificationServer *notifications, NightLight *nightLight,
                             QScreen *screen)
    : Flyout(screen)
    , m_network(network)
    , m_notifications(notifications)
    , m_nightLight(nightLight)
    , m_audio(new AudioControl(this))
    , m_brightness(new BrightnessControl(this))
    , m_bluetooth(new BluetoothControl(this))
    , m_power(new PowerProfileControl(this))
    , m_card(new QWidget(this))
    , m_grid(new QGridLayout)
{
    setWindowTitle(QStringLiteral("karyo-quick-settings"));
    const QMarginsF shadow = Ui::largeShadowMargins();
    setFixedWidth(int(shadow.left()) + CardWidth + EdgeGap);
    place(LayerSurface::AnchorBottom | LayerSurface::AnchorRight, QMargins(0, 0, 0, TaskbarHeight));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(int(shadow.left()), int(shadow.top()), EdgeGap, EdgeGap);
    outer->addWidget(m_card);

    const Ui::Theme &t = Ui::Theme::current();
    auto *pages = new QVBoxLayout(m_card);
    pages->setContentsMargins(0, 0, 0, 0);
    m_overview = new QWidget(m_card);
    pages->addWidget(m_overview);
    m_networkPage = new NetworkPage(m_network, [this] { showNetwork(false); }, [this] {
        Integrations::openSettings(true);
        close();
    }, [this] { fitHeight(); }, m_card);
    m_networkPage->hide();
    pages->addWidget(m_networkPage);

    auto *column = new QVBoxLayout(m_overview);
    column->setContentsMargins(Padding, Padding, Padding, 16);
    column->setSpacing(0);
    m_grid->setSpacing(10);
    for (int c = 0; c < 3; ++c)
        m_grid->setColumnStretch(c, 1);
    column->addLayout(m_grid);
    column->addSpacing(18);

    if (m_brightness->isAvailable()) {
        auto *row = new QHBoxLayout;
        row->setSpacing(12);
        auto *icon = new Ui::IconButton(QStringLiteral("sun"), m_card, {36, 36}, 18);
        icon->setGlyphColor(t.text);
        icon->setEnabled(false);
        row->addWidget(icon);
        auto *brightness = slider(m_card);
        brightness->setValue(m_brightness->brightness());
        connect(brightness, &QSlider::valueChanged, m_brightness, &BrightnessControl::setBrightness);
        row->addWidget(brightness, 1);
        row->addSpacing(36 + 12);
        column->addLayout(row);
        column->addSpacing(6);
    }

    m_volumeRow = new QWidget(m_card);
    auto *volumeRow = new QHBoxLayout(m_volumeRow);
    volumeRow->setContentsMargins(0, 0, 0, 0);
    volumeRow->setSpacing(12);
    m_mute = new Ui::IconButton(QStringLiteral("volume-2"), m_volumeRow, {36, 36}, 18);
    m_mute->setFocusPolicy(Qt::NoFocus);
    m_mute->setHint(tr("Mute"));
    connect(m_mute, &QAbstractButton::clicked, m_audio, &AudioControl::toggleMute);
    volumeRow->addWidget(m_mute);
    m_volume = slider(m_volumeRow);
    connect(m_volume, &QSlider::valueChanged, m_audio, &AudioControl::setVolume);
    volumeRow->addWidget(m_volume, 1);
    auto *mixer = new Ui::IconButton(QStringLiteral("chevron-right"), m_volumeRow, {36, 36}, 16);
    mixer->setFocusPolicy(Qt::NoFocus);
    mixer->setGlyphColor(t.text);
    mixer->setHint(tr("Sound settings"));
    connect(mixer, &QAbstractButton::clicked, this, [this] {
        launchFirst({QStringLiteral("pavucontrol-qt.desktop"), QStringLiteral("pavucontrol.desktop")});
        close();
    });
    volumeRow->addWidget(mixer);
    column->addWidget(m_volumeRow);
    column->addSpacing(14);

    auto *line = new QWidget(m_card);
    line->setFixedHeight(1);
    line->setAutoFillBackground(true);
    QPalette linePalette = line->palette();
    linePalette.setColor(QPalette::Window, t.edge);
    line->setPalette(linePalette);
    column->addWidget(line);
    column->addSpacing(10);

    auto *footer = new QHBoxLayout;
    footer->setSpacing(8);
    auto *speaker = new QLabel(m_card);
    speaker->setPixmap(Ui::icon(QStringLiteral("speaker"), 16, t.muted, devicePixelRatioF()));
    footer->addSpacing(6);
    footer->addWidget(speaker);
    m_output = new QLabel(m_card);
    m_output->setFont(Ui::bodyFont(12.5));
    QPalette muted = m_output->palette();
    muted.setColor(QPalette::WindowText, t.muted);
    m_output->setPalette(muted);
    footer->addWidget(m_output, 1);
    auto *settings = new Ui::IconButton(QStringLiteral("settings"), m_card, {36, 36}, 18);
    settings->setFocusPolicy(Qt::NoFocus);
    settings->setGlyphColor(t.text);
    settings->setHint(tr("All settings"));
    settings->setEnabled(!Integrations::settingsProgram(false).isEmpty());
    connect(settings, &QAbstractButton::clicked, this, [this] {
        Integrations::openSettings();
        close();
    });
    footer->addWidget(settings);
    column->addLayout(footer);

    // Queued: a tile's own click changes the state, and the tile must not be
    // deleted while it handles the click.
    constexpr auto queued = Qt::QueuedConnection;
    connect(m_network, &NetworkStatus::changed, this, &QuickSettings::rebuildTiles, queued);
    connect(m_bluetooth, &BluetoothControl::changed, this, &QuickSettings::rebuildTiles, queued);
    connect(m_power, &PowerProfileControl::changed, this, &QuickSettings::rebuildTiles, queued);
    connect(m_nightLight, &NightLight::changed, this, &QuickSettings::rebuildTiles, queued);
    connect(m_notifications, &NotificationServer::doNotDisturbChanged, this, &QuickSettings::rebuildTiles, queued);
    connect(m_audio, &AudioControl::changed, this, &QuickSettings::updateAudio);
    rebuildTiles();
    updateAudio();
}

void QuickSettings::showNetwork(bool show)
{
    m_showNetwork = show;
    m_overview->setVisible(!show);
    m_networkPage->setVisible(show);
    if (show)
        static_cast<NetworkPage *>(m_networkPage)->refresh();
    fitHeight();
}

void QuickSettings::rebuildTiles()
{
    qDeleteAll(m_tiles);
    m_tiles.clear();
    QList<QuickTile *> tiles;

    const auto moreNetwork = [this] { showNetwork(true); };
    const QString name = m_network->connectionName();
    if (m_network->hasWireless()) {
        const bool on = m_network->isWirelessEnabled();
        tiles.append(new QuickTile(QStringLiteral("wifi"), on && !name.isEmpty() ? name : tr("Wi-Fi"),
                                   !on                          ? tr("Off")
                                   : m_network->isConnected() ? tr("Connected")
                                                               : tr("Not connected"),
                                   on, {}, moreNetwork, m_card));
    } else {
        tiles.append(new QuickTile(QStringLiteral("network"), name.isEmpty() ? tr("Ethernet") : name,
                                   m_network->isConnected() ? tr("Connected") : tr("Not connected"),
                                   m_network->isConnected(), {}, moreNetwork, m_card));
    }
    const auto toggle = [](bool available, std::function<void()> action) {
        return available ? action : std::function<void()>();
    };
    const auto status = [this](bool available, bool on) {
        return !available ? tr("Unavailable") : on ? tr("On") : tr("Off");
    };
    const bool bluetooth = m_bluetooth->isPowered();
    tiles.append(new QuickTile(QStringLiteral("bluetooth"), tr("Bluetooth"),
                               status(m_bluetooth->isAvailable(), bluetooth), bluetooth,
                               toggle(m_bluetooth->isAvailable(), [this, bluetooth] { m_bluetooth->setPowered(!bluetooth); }), {}, m_card));
    const bool dnd = m_notifications->doNotDisturb();
    tiles.append(new QuickTile(QStringLiteral("bell-off"), tr("Do not disturb"), status(true, dnd), dnd,
                               [this, dnd] { m_notifications->setDoNotDisturb(!dnd); }, {}, m_card));
    const QString profile = m_power->profile();
    const bool power = m_power->isAvailable();
    tiles.append(new QuickTile(QStringLiteral("gauge"), tr("Power mode"),
                               power ? PowerProfileControl::label(profile) : tr("Unavailable"),
                               power && profile != QLatin1String("balanced"),
                               toggle(power, [this] { m_power->cycle(); }), {}, m_card));
    const bool night = m_nightLight->isOn();
    tiles.append(new QuickTile(QStringLiteral("moon"), tr("Night light"),
                               status(m_nightLight->isAvailable(), night), night,
                               toggle(m_nightLight->isAvailable(), [this, night] { m_nightLight->setOn(!night); }), {}, m_card));
    auto *accessibility = new QuickTile(QStringLiteral("accessibility"), tr("Accessibility"), tr("Screen magnifier"),
                                       false, {}, {}, m_card);
    accessibility->setEnabled(true);
    tiles.append(accessibility);
    connect(accessibility, &QAbstractButton::clicked, accessibility, [accessibility] {
        auto *menu = new QMenu(accessibility);
        Ui::styleMenu(menu);
        connect(menu, &QMenu::aboutToHide, menu, &QObject::deleteLater);
        menu->addSection(tr("Screen magnifier"));
        menu->addAction(tr("Toggle · Super+Alt+M"))->setEnabled(false);
        menu->addAction(tr("Zoom in · Super+Alt+="))->setEnabled(false);
        menu->addAction(tr("Zoom out · Super+Alt+−"))->setEnabled(false);
        menu->popup(accessibility->mapToGlobal(accessibility->rect().bottomLeft()));
    });

    for (int i = 0; i < tiles.size(); ++i) {
        m_grid->addWidget(tiles.at(i), i / 3, i % 3);
        // Shown now, not later as layouts do: fitHeight() counts visible tiles.
        tiles.at(i)->show();
        m_tiles.append(tiles.at(i));
    }
    fitHeight();
}

void QuickSettings::updateAudio()
{
    m_volumeRow->setVisible(m_audio->isAvailable());
    const QSignalBlocker block(m_volume);
    m_volume->setValue(m_audio->volume());
    m_mute->setGlyphColor(m_audio->isMuted() ? Ui::Theme::current().muted : Ui::Theme::current().text);
    m_mute->setHint(m_audio->isMuted() ? tr("Unmute") : tr("Mute"));
    m_output->setText(m_audio->outputName().isEmpty() ? QString()
                                                      : tr("Output: %1").arg(m_audio->outputName()));
    fitHeight();
}

void QuickSettings::fitHeight()
{
    const QMarginsF shadow = Ui::largeShadowMargins();
    m_card->layout()->activate();
    setFixedHeight(int(shadow.top()) + m_card->layout()->sizeHint().height() + EdgeGap);
}

void QuickSettings::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    paintCard(painter, m_card->geometry(), CardRadius);
}

void QuickSettings::mousePressEvent(QMouseEvent *event)
{
    if (!m_card->geometry().contains(event->position().toPoint())) {
        close();
        return;
    }
    Flyout::mousePressEvent(event);
}
