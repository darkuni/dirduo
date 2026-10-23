#include "core/Prefs.h"

#include "core/Paths.h"

#include <QSettings>

Prefs *Prefs::instance()
{
    static Prefs p;
    return &p;
}

Prefs::Prefs()
{
    reload();
}

void Prefs::reload()
{
    QSettings s(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("General"));
    Values v;
    v.statusCounts = s.value(QStringLiteral("statusCounts"), true).toBool();
    v.statusSelection = s.value(QStringLiteral("statusSelection"), true).toBool();
    v.statusFree = s.value(QStringLiteral("statusFree"), true).toBool();
    v.askOnError = s.value(QStringLiteral("askOnError"), true).toBool();
    v.terminal = s.value(QStringLiteral("terminal")).toString();
    m_values = v;
}

void Prefs::setValues(const Values &v)
{
    m_values = v;
    QSettings s(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("General"));
    s.setValue(QStringLiteral("statusCounts"), v.statusCounts);
    s.setValue(QStringLiteral("statusSelection"), v.statusSelection);
    s.setValue(QStringLiteral("statusFree"), v.statusFree);
    s.setValue(QStringLiteral("askOnError"), v.askOnError);
    s.setValue(QStringLiteral("terminal"), v.terminal);
    Q_EMIT changed();
}
