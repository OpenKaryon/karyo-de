// SPDX-License-Identifier: GPL-3.0-or-later
// The sections of Settings, in the order of the navigation.
#pragma once

#include <functional>

class QObject;
class QWidget;
class SettingsPage;

SettingsPage *createSystemPage(QWidget *parent);
SettingsPage *createStoragePage(QWidget *parent);  // System › Storage
SettingsPage *createDevicesPage(QWidget *parent);
SettingsPage *createNetworkPage(QWidget *parent);
SettingsPage *createPersonalizationPage(QWidget *parent);
SettingsPage *createAppsPage(QWidget *parent);
SettingsPage *createAccountsPage(QWidget *parent);
SettingsPage *createTimeLanguagePage(QWidget *parent);
SettingsPage *createPrivacyPage(QWidget *parent);
SettingsPage *createUpdatesPage(QWidget *parent);
SettingsPage *createAboutPage(QWidget *parent);

// Upgradable packages from apt's cached lists, for the Updates badge.
// Calls back with -1 if apt cannot tell.
void countUpdates(QObject *context, const std::function<void(int)> &done);
