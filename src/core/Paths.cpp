#include "core/Paths.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>

namespace {
QString g_configOverride;
QString g_dataOverride;
QString g_cacheOverride;

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

QString userCacheHome()
{
    if (!g_cacheOverride.isEmpty())
        return g_cacheOverride;
    return xdgOrDefault("XDG_CACHE_HOME", QStringLiteral(".cache"));
}

QString configDir()
{
    const QString dir = g_configOverride.isEmpty()
        ? userConfigHome() + QStringLiteral("/dirduo")
        : g_configOverride;
    QDir().mkpath(dir);
    return dir;
}

namespace {
bool copyTree(const QString &from, const QString &to)
{
    bool any = false;
    QDirIterator it(from, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString src = it.next();
        QString rel = src.mid(from.size() + 1);
        if (rel == QLatin1String("linopus.ini"))
            rel = QStringLiteral("dirduo.ini");
        const QString dst = to + QLatin1Char('/') + rel;
        const QFileInfo fi(src);
        if (fi.isDir() && !fi.isSymLink()) {
            QDir().mkpath(dst);
        } else if (!QFileInfo::exists(dst)) {
            QDir().mkpath(QFileInfo(dst).absolutePath());
            any = QFile::copy(src, dst) || any;
        }
    }
    return any;
}
} // namespace

bool migrateFromLinopus()
{
    if (!g_configOverride.isEmpty())
        return false; // tests
    bool any = false;
    const QString oldConfig = userConfigHome() + QStringLiteral("/linopus");
    const QString newConfig = userConfigHome() + QStringLiteral("/dirduo");
    if (QFileInfo(oldConfig).isDir() && !QFileInfo::exists(newConfig + QStringLiteral("/dirduo.ini"))) {
        QDir().mkpath(newConfig);
        any = copyTree(oldConfig, newConfig) || any;
    }
    const QString oldData = userDataHome() + QStringLiteral("/linopus");
    const QString newData = userDataHome() + QStringLiteral("/dirduo");
    if (QFileInfo(oldData).isDir() && !QFileInfo(newData).isDir()) {
        QDir().mkpath(newData);
        any = copyTree(oldData, newData) || any;
    }
    return any;
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

void setCacheHomeOverride(const QString &dir)
{
    g_cacheOverride = dir;
}

} // namespace Paths
