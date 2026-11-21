// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QRegularExpression>
#include <QString>
namespace Karyo::Accounts::Internal
{
enum class ReadState
{
    Found,
    Missing,
    Invalid
};
struct ConfigText
{
    ReadState state = ReadState::Invalid;
    QString text;
};
ConfigText readRootConfig(const QString &path);
ConfigText readConfig(const QString &current, const QString &legacy = { });
bool validLogin(const QString &login);
bool boundedText(const QString &text);
const QRegularExpression &lineBreaks();
const QRegularExpression &whitespace();
QString stripped(QString text);
} // namespace Karyo::Accounts::Internal
