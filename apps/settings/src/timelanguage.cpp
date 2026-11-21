// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language (timedated, the session's language and keyboard layouts)
// and Privacy & security (recent files, file search in Start).
#include "config.h"
#include "pages.h"
#include "widgets.h"

#include <karyo/ui/theme.h>

#include <QComboBox>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QSignalBlocker>
#include <QSaveFile>
#include <QDBusVariant>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimeZone>
#include <QTimer>
#include <QVBoxLayout>

namespace Ui = Karyo::Ui;

namespace {

const QString Timedate = QStringLiteral("org.freedesktop.timedate1");

// Interactive authorization is handled by timedated/polkit, without blocking
// the page or treating a queued request as a successful state change.
void callTimedate(QObject *context, const QString &method, const QVariantList &args,
                  const std::function<void(const QString &)> &done)
{
    auto call = QDBusMessage::createMethodCall(Timedate, QStringLiteral("/org/freedesktop/timedate1"), Timedate, method);
    call.setInteractiveAuthorizationAllowed(true);
    call.setArguments(args);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 120000), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, done] {
        const QDBusPendingReply<> reply = *watcher;
        const QString error = reply.isError() ? reply.error().message() : QString();
        watcher->deleteLater();
        done(error);
    });
}

QString sessionOr(const QString &key, const QString &fallback)
{
    const QString value = Config::sessionValue(key);
    return value.isEmpty() ? fallback : value;
}

class TimeLanguagePage : public SettingsPage
{
    Q_OBJECT
public:
    explicit TimeLanguagePage(QWidget *parent)
        : SettingsPage(tr("Time & language"), parent)
    {
        addKeywords({tr("Time zone"), tr("Language"), tr("Keyboard"), tr("Layouts"), QStringLiteral("timezone"),
                     QStringLiteral("keyboard")});
        m_clock = new QLabel(this);
        m_clock->setFont(Ui::headingFont(30));
        column()->addWidget(m_clock);
        auto *tick = new QTimer(this);
        connect(tick, &QTimer::timeout, this, &TimeLanguagePage::updateClock);
        tick->start(1000);
        updateClock();

        m_sessionStatus = new QLabel(this);
        m_sessionStatus->setObjectName("sessionSettingStatus");
        m_sessionStatus->setWordWrap(true); m_sessionStatus->setTextFormat(Qt::PlainText);
        column()->addWidget(m_sessionStatus);
        m_timeStatus = new QLabel(tr("Reading date and time settings…"), this);
        m_timeStatus->setObjectName("timedateStatus");
        m_timeStatus->setWordWrap(true); m_timeStatus->setTextFormat(Qt::PlainText);
        column()->addWidget(sectionHeading(tr("Date & time")));
        column()->addWidget(m_timeStatus);
        auto *ntp = new SettingRow(QStringLiteral("history"), tr("Set time automatically"),
                                   tr("From time servers on the internet"), this);
        auto *ntpToggle = m_ntp = ntp->addToggle(false);
        ntpToggle->setObjectName("automaticTime"); ntpToggle->setEnabled(false);
        connect(ntpToggle, &QAbstractButton::toggled, this,
                [this](bool on) { applyTimedate(QStringLiteral("SetNTP"), {on, true}); });
        column()->addWidget(ntp);

        auto *zone = new SettingRow(QStringLiteral("globe"), tr("Time zone"), QString(), this);
        auto *zones = settingCombo(); m_zones = zones;
        zones->setObjectName("timeZone"); zones->setEnabled(false);
        zones->setMinimumWidth(280);
        for (const QByteArray &id : QTimeZone::availableTimeZoneIds()) {
            const QString name = QString::fromLatin1(id);
            if (!name.contains(QLatin1Char('/')) || name.startsWith(QLatin1String("Etc/")))
                continue;
            zones->addItem(QString(name).replace(QLatin1Char('_'), QLatin1Char(' ')), name);

        }
        zones->addItem(QStringLiteral("UTC"), QStringLiteral("UTC"));
        connect(zones, &QComboBox::activated, this, [this, zones](int index) {
            applyTimedate(QStringLiteral("SetTimezone"), {zones->itemData(index).toString(), true});
        });
        zone->setControl(zones);
        column()->addWidget(zone);

        column()->addWidget(sectionHeading(tr("Language")));
        auto *language = new SettingRow(QStringLiteral("languages"), tr("Display language"),
                                        tr("Apps and the desktop use it after you sign in again"), this);
        auto *languages = settingCombo();
        languages->setMinimumWidth(320);
        languages->setObjectName("displayLanguage"); languages->setEnabled(false);
        const QString lang = sessionOr(QStringLiteral("LANG"), qEnvironmentVariable("LANG"));
        runCommand(this, QStringLiteral("locale"), {QStringLiteral("-a")}, [this, languages, language, lang](const QString &out) {
            for (const QString &raw : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                const QString lower = raw.toLower();
                if ((!lower.endsWith(QLatin1String(".utf8")) && !lower.endsWith(QLatin1String(".utf-8"))) || !raw.contains(QLatin1Char('_')))
                    continue;
                const QString code = raw.section(QLatin1Char('.'), 0, 0);
                const QLocale locale(code);
                QString name = locale.nativeLanguageName();
                if (!name.isEmpty())
                    name[0] = name.at(0).toUpper();
                name += QStringLiteral(" (") + locale.nativeTerritoryName() + QLatin1Char(')');
                const QString value = code + QStringLiteral(".UTF-8");
                languages->addItem(name, value);
                if (lang.startsWith(code))
                    languages->setCurrentIndex(languages->count() - 1);
            }
            languages->setEnabled(languages->count() > 0);
            m_languageIndex = languages->currentIndex();
            if (languages->count() == 0)
                language->setDescription(tr("No additional UTF-8 locales are installed."));
        }, [language](const QString &error) { language->setDescription(error); });
        connect(languages, &QComboBox::activated, this, [this, languages](int index) {
            if (saveSession(QStringLiteral("LANG"), languages->itemData(index).toString()))
                m_languageIndex = index;
            else { const QSignalBlocker blocker(languages); languages->setCurrentIndex(m_languageIndex); }
        });
        language->setControl(languages);
        column()->addWidget(language);

        column()->addWidget(sectionHeading(tr("Keyboard")));
        m_layouts = new QVBoxLayout;
        m_layouts->setSpacing(10);
        column()->addLayout(m_layouts);

        auto *add = new SettingRow(QStringLiteral("circle-plus"), tr("Add a layout"), QString(), this);
        m_add = settingCombo(); m_add->setObjectName("addKeyboardLayout");
        for (const auto &[code, name] : knownLayouts())
            m_add->addItem(name, code);
        auto *addButton = new QPushButton(tr("Add")); addButton->setObjectName("addLayout");
        connect(addButton, &QPushButton::clicked, this, [this] {
            QStringList layouts = currentLayouts();
            const QString code = m_add->currentData().toString();
            if (!layouts.contains(code)) {
                layouts.append(code);
                saveLayouts(layouts);
            }
        });
        add->setControl(m_add);
        add->setControl(addButton);
        column()->addWidget(add);

        auto *shortcut = new SettingRow(QStringLiteral("key-round"), tr("Switch layouts with"), QString(), this);
        auto *shortcuts = settingCombo(); shortcuts->setObjectName("layoutShortcut");
        const QList<QPair<QString, QString>> options = {{QStringLiteral("grp:win_space_toggle"), tr("Super + Space")},
                                                        {QStringLiteral("grp:alt_shift_toggle"), tr("Alt + Shift")},
                                                        {QStringLiteral("grp:ctrl_shift_toggle"), tr("Ctrl + Shift")},
                                                        {QStringLiteral("grp:caps_toggle"), tr("Caps Lock")}};
        const QString xkbOptions = sessionOr(QStringLiteral("XKB_DEFAULT_OPTIONS"), qEnvironmentVariable("XKB_DEFAULT_OPTIONS"));
        for (const auto &[value, name] : options) {
            shortcuts->addItem(name, value);
            if (xkbOptions.contains(value))
                shortcuts->setCurrentIndex(shortcuts->count() - 1);
        }
        m_shortcutIndex = shortcuts->currentIndex();
        connect(shortcuts, &QComboBox::activated, this, [this, shortcuts](int index) {
            const QString xkbOptions = sessionOr(QStringLiteral("XKB_DEFAULT_OPTIONS"), qEnvironmentVariable("XKB_DEFAULT_OPTIONS"));
            // Keep options other than the group switch.
            QStringList kept;
            for (const QString &option : xkbOptions.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                if (!option.startsWith(QLatin1String("grp:")))
                    kept.append(option);
            }
            kept.append(shortcuts->itemData(index).toString());
            if (saveSession(QStringLiteral("XKB_DEFAULT_OPTIONS"), kept.join(QLatin1Char(','))))
                m_shortcutIndex = index;
            else { const QSignalBlocker blocker(shortcuts); shortcuts->setCurrentIndex(m_shortcutIndex); }
        });
        shortcut->setControl(shortcuts);
        column()->addWidget(shortcut);
        column()->addWidget(note(tr("Language and keyboard changes take effect the next time you sign in.")));
        showLayouts();
        readTimedate();
    }

private:
    bool saveSession(const QString &key, const QString &value)
    {
        QString error;
        if (Config::setSessionValue(key, value, &error)) { m_sessionStatus->clear(); return true; }
        m_sessionStatus->setText(tr("Could not save settings: %1").arg(error));
        return false;
    }
    void applyTimedate(const QString &method, const QVariantList &arguments)
    {
        m_ntp->setEnabled(false); m_zones->setEnabled(false);
        m_timeStatus->setText(tr("Applying date and time settings…"));
        callTimedate(this, method, arguments, [this](const QString &error) { readTimedate(error); });
    }
    void readTimedate(const QString &operationError = {})
    {
        auto call = QDBusMessage::createMethodCall(Timedate, QStringLiteral("/org/freedesktop/timedate1"),
            QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("GetAll"));
        call << Timedate;
        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call, 5000), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, operationError] {
            const QDBusPendingReply<QVariantMap> reply = *watcher;
            watcher->deleteLater();
            if (reply.isError()) {
                m_timeStatus->setText(tr("Date and time settings are unavailable: %1").arg(reply.error().message()));
                return;
            }
            const QSignalBlocker blocker(m_ntp);
            m_ntp->setChecked(reply.value().value(QStringLiteral("NTP")).toBool());
            const QString zone = reply.value().value(QStringLiteral("Timezone")).toString();
            int index = m_zones->findData(zone);
            if (index < 0 && !zone.isEmpty()) { m_zones->addItem(zone, zone); index = m_zones->count() - 1; }
            m_zones->setCurrentIndex(index);
            m_ntp->setEnabled(true); m_zones->setEnabled(index >= 0);
            m_timeStatus->setText(operationError);
        });
    }
    static QList<QPair<QString, QString>> knownLayouts()
    {
        return {{QStringLiteral("us"), tr("English (US)")},   {QStringLiteral("gb"), tr("English (UK)")},
                {QStringLiteral("ru"), tr("Russian")},        {QStringLiteral("ua"), tr("Ukrainian")},
                {QStringLiteral("by"), tr("Belarusian")},     {QStringLiteral("kz"), tr("Kazakh")},
                {QStringLiteral("de"), tr("German")},         {QStringLiteral("fr"), tr("French")},
                {QStringLiteral("es"), tr("Spanish")},        {QStringLiteral("it"), tr("Italian")},
                {QStringLiteral("pl"), tr("Polish")},         {QStringLiteral("cz"), tr("Czech")},
                {QStringLiteral("tr"), tr("Turkish")},        {QStringLiteral("cn"), tr("Chinese")},
                {QStringLiteral("jp"), tr("Japanese")}};
    }

    QStringList currentLayouts() const
    {
        return sessionOr(QStringLiteral("XKB_DEFAULT_LAYOUT"), qEnvironmentVariable("XKB_DEFAULT_LAYOUT", QStringLiteral("us")))
            .split(QLatin1Char(','), Qt::SkipEmptyParts);
    }

    void saveLayouts(const QStringList &layouts)
    {
        if (saveSession(QStringLiteral("XKB_DEFAULT_LAYOUT"), layouts.join(QLatin1Char(','))))
            showLayouts();
    }

    void showLayouts()
    {
        while (QLayoutItem *item = m_layouts->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        const QStringList layouts = currentLayouts();
        for (const QString &code : layouts) {
            QString name = code;
            for (const auto &[known, label] : knownLayouts()) {
                if (known == code)
                    name = label;
            }
            auto *row = new SettingRow(QStringLiteral("type"), name, code.toUpper(), this);
            if (layouts.size() > 1) {
                auto *remove = new QPushButton(tr("Remove"));
                connect(remove, &QPushButton::clicked, this, [this, code] {
                    QStringList left = currentLayouts();
                    left.removeAll(code);
                    saveLayouts(left);
                });
                row->setControl(remove);
            }
            m_layouts->addWidget(row);
        }
    }

    void updateClock()
    {
        const QLocale locale;
        const QDateTime now = QDateTime::currentDateTime();
        m_clock->setText(locale.toString(now.time(), QLocale::ShortFormat) + QStringLiteral("  ·  ")
                         + locale.toString(now.date(), QLocale::LongFormat));
    }

    QLabel *m_clock;
    QVBoxLayout *m_layouts;
    QComboBox *m_add;
    ToggleSwitch *m_ntp = nullptr;
    QComboBox *m_zones = nullptr;
    QLabel *m_timeStatus = nullptr, *m_sessionStatus = nullptr;
    int m_languageIndex = -1, m_shortcutIndex = 0;
};

class PrivacyPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit PrivacyPage(QWidget *parent)
        : SettingsPage(tr("Privacy & security"), parent)
    {
        addKeywords({tr("Recent files"), tr("History"), QStringLiteral("privacy")});
        const QSettings shell = Config::shell();
        column()->addWidget(sectionHeading(tr("Activity")));
        auto *recent = new SettingRow(QStringLiteral("history"), tr("Show recently opened files"),
                                      tr("In Start's Recommended list"), this);
        connect(recent->addToggle(shell.value(QStringLiteral("Start/showRecent"), true).toBool()), &QAbstractButton::toggled,
                this, [](bool on) { Config::shell().setValue(QStringLiteral("Start/showRecent"), on); });
        column()->addWidget(recent);

        auto *clear = new SettingRow(QStringLiteral("trash-2"), tr("Recent files history"),
                                     tr("The list apps keep of files you opened"), this);
        auto *clearButton = new QPushButton(tr("Clear")); clearButton->setObjectName("clearRecentHistory");
        connect(clearButton, &QPushButton::clicked, this, [clear, clearButton] {
            const QString path = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + QStringLiteral("/recently-used.xbel");
            if (QFileInfo::exists(path)) {
                QSaveFile file(path);
                const QByteArray empty("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xbel version=\"1.0\"/>\n");
                if (!file.open(QIODevice::WriteOnly) || file.write(empty) != empty.size() || !file.commit()) {
                    clear->setValue(tr("Could not clear history: %1").arg(file.errorString()));
                    return;
                }
            }
            clear->setValue(tr("Cleared"));
            clearButton->setEnabled(false);
        });
        clear->setControl(clearButton);
        column()->addWidget(clear);

        auto *search = new SettingRow(QStringLiteral("search"), tr("Search files in Start"),
                                      tr("Start looks through file names in your home folder"), this);
        connect(search->addToggle(shell.value(QStringLiteral("Start/searchFiles"), true).toBool()), &QAbstractButton::toggled,
                this, [](bool on) { Config::shell().setValue(QStringLiteral("Start/searchFiles"), on); });
        column()->addWidget(search);

        column()->addWidget(sectionHeading(tr("Security")));
        column()->addWidget(new SettingRow(
            QStringLiteral("lock"), tr("Administrator actions"),
            Config::isAdministrator() ? tr("You are an administrator; system changes ask for your password")
                                      : tr("System changes ask for an administrator's password"),
            this));
    }
};

} // namespace

SettingsPage *createTimeLanguagePage(QWidget *parent) { return new TimeLanguagePage(parent); }
SettingsPage *createPrivacyPage(QWidget *parent) { return new PrivacyPage(parent); }

#include "timelanguage.moc"
