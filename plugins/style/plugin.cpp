// SPDX-License-Identifier: GPL-3.0-or-later
// Loads the Karyo style into any Qt 6 application: QT_STYLE_OVERRIDE=Karyo.
#include <karyo/ui/style.h>

#include <QStylePlugin>

class KaryoStylePlugin : public QStylePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QStyleFactoryInterface_iid FILE "karyo.json")
public:
    QStyle *create(const QString &key) override
    {
        if (key.compare(QLatin1String("Karyo"), Qt::CaseInsensitive) == 0)
            return new Karyo::Ui::KaryoStyle;
        return nullptr;
    }
};

#include "plugin.moc"
