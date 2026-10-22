#include "core/Paths.h"

#include <QDir>

namespace {
QString g_configOverride;
QString g_dataOverride;

QString xdgOrDefault(const char *var, const QString &fallbackRelative)
{
    if (!Paths::insideFlatpak()) {
        const QString v = qEnvironmentVariable(var);
        if (!v.isEmpty() && QDir::isAbsolutePath(v))
            return QDir::cleanPath(v);
    }
    return QDir::homePath() + QLatin1Char('/') + fallbackRelative;
}
} // namespace

namespace Paths {

bool insideFlatpak()
{
    return qEnvironmentVariableIsSet("FLATPAK_ID");
}

QString userConfigHome()
{
    return xdgOrDefault("XDG_CONFIG_HOME", QStringLiteral(".config"));
}

QString userDataHome()
{
    if (!g_dataOverride.isEmpty())
        return g_dataOverride;
    return xdgOrDefault("XDG_DATA_HOME", QStringLiteral(".local/share"));
}

QString configDir()
{
    const QString dir = g_configOverride.isEmpty()
        ? userConfigHome() + QStringLiteral("/linopus")
        : g_configOverride;
    QDir().mkpath(dir);
    return dir;
}

QString configFile(const QString &name)
{
    return configDir() + QLatin1Char('/') + name;
}

void setConfigDirOverride(const QString &dir)
{
    g_configOverride = dir;
}

void setDataHomeOverride(const QString &dir)
{
    g_dataOverride = dir;
}

} // namespace Paths
