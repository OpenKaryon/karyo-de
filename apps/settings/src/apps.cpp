// SPDX-License-Identifier: GPL-3.0-or-later
// Apps (default apps through mimeapps.list, installed apps) and Accounts.
#include "config.h"
#include "pages.h"
#include "widgets.h"

#include <karyo/ui/desktopentry.h>
#include "desktopaccounts.h"
#include "desktopsettings.h"
#include "defaultapps.h"
#include <QMessageBox>
#include <QSignalBlocker>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

#include <algorithm>

#include <pwd.h>
#include <unistd.h>

namespace {

class AppsPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit AppsPage(QWidget *parent)
        : SettingsPage(tr("Apps"), parent)
    {
        addKeywords({tr("Default apps"), tr("Web browser"), tr("Installed apps"), QStringLiteral("browser")});
        column()->addWidget(sectionHeading(tr("Default apps")));
        struct Kind
        {
            QString glyph;
            QString name;
            QStringList mimeTypes;
        };
        const QList<Kind> kinds = {
            {QStringLiteral("globe"), tr("Web browser"),
             {QStringLiteral("x-scheme-handler/http"), QStringLiteral("x-scheme-handler/https"), QStringLiteral("text/html")}},
            {QStringLiteral("folder"), tr("Files"), {QStringLiteral("inode/directory")}},
            {QStringLiteral("file-pen-line"), tr("Text editor"), {QStringLiteral("text/plain")}},
            {QStringLiteral("image"), tr("Image viewer"),
             {QStringLiteral("image/png"), QStringLiteral("image/jpeg"), QStringLiteral("image/gif"),
              QStringLiteral("image/webp"), QStringLiteral("image/svg+xml")}},
            {QStringLiteral("file-text"), tr("PDF documents"), {QStringLiteral("application/pdf")}},
            {QStringLiteral("mail"), tr("Email"), {QStringLiteral("x-scheme-handler/mailto")}},
        };
        for (const Kind &kind : kinds) {
            const QList<DesktopEntry> candidates = DesktopEntry::forMimeType(kind.mimeTypes.first());
            auto *row = new SettingRow(kind.glyph, kind.name, QString(), this);
            if (candidates.isEmpty()) {
                row->setValue(tr("No app installed"));
                column()->addWidget(row);
                continue;
            }
            auto *combo = settingCombo();
            const auto current = DesktopEntry::defaultFor(kind.mimeTypes.first());
            for (const DesktopEntry &entry : candidates) {
                combo->addItem(entry.name, entry.id);
                if (current && current->id == entry.id)
                    combo->setCurrentIndex(combo->count() - 1);
            }
            const QStringList types = kind.mimeTypes;
            connect(combo, &QComboBox::activated, this,
                    [combo, types, previous = combo->currentIndex()](int index) mutable {
                        QString error;
                        if (DefaultApps::set(types, combo->itemData(index).toString(), &error)) {
                            previous = index;
                            return;
                        }
                        const QSignalBlocker blocker(combo);
                        combo->setCurrentIndex(previous);
                        auto *message = new QMessageBox(QMessageBox::Warning, tr("Could not change the default app"),
                            error, QMessageBox::Ok, combo);
                        message->setTextFormat(Qt::PlainText);
                        message->setAttribute(Qt::WA_DeleteOnClose);
                        message->open();
                    });
            row->setControl(combo);
            column()->addWidget(row);
        }

        column()->addWidget(sectionHeading(tr("Installed apps")));
        auto *filter = new QLineEdit(this);
        filter->setPlaceholderText(tr("Search apps"));
        filter->setClearButtonEnabled(true);
        column()->addWidget(filter);
        m_list = new QVBoxLayout;
        m_list->setSpacing(10);
        column()->addLayout(m_list);
        connect(filter, &QLineEdit::textChanged, this, &AppsPage::showApps);
        showApps(QString());
    }

private:
    void showApps(const QString &filter)
    {
        while (QLayoutItem *item = m_list->takeAt(0)) {
            delete item->widget();
            delete item;
        }
        QList<DesktopEntry> entries = DesktopEntry::all();
        std::sort(entries.begin(), entries.end(), [](const DesktopEntry &a, const DesktopEntry &b) {
            return QString::localeAwareCompare(a.name, b.name) < 0;
        });
        for (const DesktopEntry &entry : std::as_const(entries)) {
            if (entry.noDisplay || entry.exec.isEmpty())
                continue;
            if (!filter.isEmpty() && !entry.name.contains(filter, Qt::CaseInsensitive))
                continue;
            auto *row = new SettingRow(entry.terminal ? QStringLiteral("square-terminal") : QStringLiteral("app-window"),
                                       entry.name, entry.comment.isEmpty() ? entry.genericName : entry.comment, this);
            auto *open = new QPushButton(tr("Open"));
            connect(open, &QPushButton::clicked, this, [entry] { entry.launch(); });
            row->setControl(open);
            m_list->addWidget(row);
        }
    }

    QVBoxLayout *m_list;
};

class AccountsPage : public SettingsPage
{
    Q_OBJECT
public:
    explicit AccountsPage(QWidget *parent)
        : SettingsPage(tr("Accounts"), parent)
    {
        addKeywords({tr("Password"), tr("Your info"), tr("Other users"), tr("Desktops")});
        column()->addWidget(sectionHeading(tr("Your info")));
        auto *info = new FactsCard(this);
        const passwd *pw = getpwuid(getuid());
        const auto accounts = Karyo::Accounts::DesktopAccounts::load();
        const auto protection = Karyo::Accounts::DesktopSettings::load(
            pw ? QString::fromLocal8Bit(pw->pw_name) : QString());
        if (!accounts.valid() || !protection.valid())
            column()->addWidget(note(tr("Desktop account configuration is unavailable."), this));
        const QString owner = accounts.ownerOf(Config::userLogin());
        const bool child = owner != Config::userLogin();
        info->setFacts({{tr("Name"), Config::userName()},
                        {tr("User name"), Config::userLogin()},
                        {tr("Account type"), Config::isAdministrator() ? tr("Administrator") : tr("Standard user")},
                        {tr("Home folder"), QDir::homePath()},
                        {tr("Shell"), pw ? QString::fromLocal8Bit(pw->pw_shell) : QString()}});
        column()->addWidget(info);

        const QString passwordMode = protection.passwordMode();
        const QString passwordDetail = child && protection.valid() && passwordMode != QLatin1String("separate")
            ? tr("Shared with the main account %1").arg(owner)
            : tr("Change it in a terminal; you type the current one first");
        auto *password = new SettingRow(QStringLiteral("key-round"), tr("Password"), passwordDetail, this);
        auto *change = new QPushButton(tr("Change"));
        connect(change, &QPushButton::clicked, this, [] { runInTerminal(QStringLiteral("passwd")); });
        password->setControl(change);
        column()->addWidget(password);

        // Karyo desktops (openkaryon-desktops): this desktop locks when it is
        // left, so coming back asks for the password. Off, switching is instant.
        column()->addWidget(sectionHeading(tr("Desktops")));
        const bool protectedDesktop = protection.protectedEntry();
        auto *lock = new SettingRow(QStringLiteral("lock"), tr("Ask for the password when switching here"),
                                    !protection.valid() ? tr("Desktop account configuration is unavailable.")
                                    : protectedDesktop ? tr("Password protection was enabled for this desktop")
                                                     : tr("Ctrl+Alt+Left/Right and the taskbar switch desktops"), this);
        auto *toggle = lock->addToggle(protectedDesktop
                                      || Config::shell().value(QStringLiteral("Desktops/lockOnLeave"), false).toBool());
        toggle->setEnabled(protection.valid() && !protectedDesktop);
        connect(toggle, &QAbstractButton::toggled, this,
                [](bool on) { Config::shell().setValue(QStringLiteral("Desktops/lockOnLeave"), on); });
        column()->addWidget(lock);

        column()->addWidget(sectionHeading(tr("Other users")));
        bool any = false;
        setpwent();
        while (const passwd *other = getpwent()) {
            const QString shell = QString::fromLocal8Bit(other->pw_shell);
            if (other->pw_uid < 1000 || other->pw_uid >= 60000 || other->pw_uid == getuid()
                || shell.endsWith(QLatin1String("nologin")) || shell.endsWith(QLatin1String("false"))
                || accounts.isChild(QString::fromLocal8Bit(other->pw_name))
                || QString::fromLocal8Bit(other->pw_name) == owner)
                continue;
            const QString name = QString::fromLocal8Bit(other->pw_gecos).section(QLatin1Char(','), 0, 0);
            column()->addWidget(new SettingRow(QStringLiteral("circle-user-round"),
                                               name.isEmpty() ? QString::fromLocal8Bit(other->pw_name) : name,
                                               QString::fromLocal8Bit(other->pw_name), this));
            any = true;
        }
        endpwent();
        if (!any)
            column()->addWidget(note(tr("You are the only user on this computer.")));
    }
};

} // namespace

SettingsPage *createAppsPage(QWidget *parent) { return new AppsPage(parent); }
SettingsPage *createAccountsPage(QWidget *parent) { return new AccountsPage(parent); }

#include "apps.moc"
