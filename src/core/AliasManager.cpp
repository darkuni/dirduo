#include "core/AliasManager.h"

#include "core/Location.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>
#include <QStorageInfo>
#include <QTextStream>
#include <QUrl>

#include <unistd.h>

namespace {

// Directories the Filesystem Hierarchy Standard puts in "/". Reserved even when
// they do not exist on this machine, so an alias written here works the same
// everywhere.
const QStringList kFhsRoots = {
    QStringLiteral("bin"), QStringLiteral("boot"), QStringLiteral("dev"), QStringLiteral("etc"),
    QStringLiteral("home"), QStringLiteral("lib"), QStringLiteral("lib32"), QStringLiteral("lib64"),
    QStringLiteral("libx32"), QStringLiteral("media"), QStringLiteral("mnt"), QStringLiteral("opt"),
    QStringLiteral("proc"), QStringLiteral("root"), QStringLiteral("run"), QStringLiteral("sbin"),
    QStringLiteral("srv"), QStringLiteral("sys"), QStringLiteral("tmp"), QStringLiteral("usr"),
    QStringLiteral("var"), QStringLiteral("app"), QStringLiteral("snap"), QStringLiteral("nix"),
};

QString userName()
{
    QString name = qEnvironmentVariable("USER");
    if (name.isEmpty())
        name = QFileInfo(QDir::homePath()).fileName();
    return name;
}

// Reads ~/.config/user-dirs.dirs directly. Inside a Flatpak, QStandardPaths
// would read the sandbox copy, which is not where the user's folders are.
QMap<QString, QString> readUserDirs()
{
    QMap<QString, QString> dirs;
    QFile f(Paths::userConfigHome() + QStringLiteral("/user-dirs.dirs"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return dirs;
    static const QRegularExpression line(QStringLiteral(R"re(^\s*XDG_([A-Z]+)_DIR\s*=\s*"(.*)"\s*$)re"));
    QTextStream in(&f);
    while (!in.atEnd()) {
        const auto m = line.match(in.readLine());
        if (!m.hasMatch())
            continue;
        QString value = m.captured(2);
        value.replace(QStringLiteral("$HOME"), QDir::homePath());
        if (!QDir::isAbsolutePath(value))
            continue;
        dirs.insert(m.captured(1), QDir::cleanPath(value));
    }
    return dirs;
}

} // namespace

AliasManager::AliasManager(const QString &configFile)
    : m_file(configFile)
{
    buildBuiltIns();
    load();
}

void AliasManager::buildBuiltIns()
{
    const QString home = QDir::homePath();
    const auto userDirs = readUserDirs();
    auto userDir = [&](const char *key, const char *fallback) {
        const QString k = QString::fromLatin1(key);
        if (userDirs.contains(k) && userDirs.value(k) != home)
            return userDirs.value(k);
        return home + QLatin1Char('/') + QString::fromLatin1(fallback);
    };

    m_builtIns.insert(QStringLiteral("desktop"), userDir("DESKTOP", "Desktop"));
    m_builtIns.insert(QStringLiteral("documents"), userDir("DOCUMENTS", "Documents"));
    m_builtIns.insert(QStringLiteral("downloads"), userDir("DOWNLOAD", "Downloads"));
    m_builtIns.insert(QStringLiteral("music"), userDir("MUSIC", "Music"));
    m_builtIns.insert(QStringLiteral("pictures"), userDir("PICTURES", "Pictures"));
    m_builtIns.insert(QStringLiteral("videos"), userDir("VIDEOS", "Videos"));
    m_builtIns.insert(QStringLiteral("templates"), userDir("TEMPLATES", "Templates"));
    m_builtIns.insert(QStringLiteral("config"), Paths::userConfigHome());
    m_builtIns.insert(QStringLiteral("dirduo"), Paths::configDir());
    m_builtIns.insert(QStringLiteral("trash"), Paths::userDataHome() + QStringLiteral("/Trash/files"));
    m_builtIns.insert(QStringLiteral("collections"), QStringLiteral("coll:/"));
    m_builtIns.insert(QStringLiteral("steam"), Paths::userDataHome() + QStringLiteral("/Steam"));
    m_builtIns.insert(QStringLiteral("temp"), QDir::tempPath());
}

// Aliases whose target depends on what is plugged in right now.
QString AliasManager::dynamicAlias(const QString &key) const
{
    const QString user = userName();
    const QString runMedia = QStringLiteral("/run/media/") + user;
    const QString media = QStringLiteral("/media/") + user;

    if (key == QLatin1String("removable")) {
        if (QFileInfo(runMedia).isDir())
            return runMedia;
        if (QFileInfo(media).isDir())
            return media;
        return {};
    }
    if (key == QLatin1String("sdcard")) {
        // The Deck mounts its microSD card from /dev/mmcblk0p1.
        const auto volumes = QStorageInfo::mountedVolumes();
        for (const QStorageInfo &v : volumes) {
            if (v.isValid() && v.isReady()
                && QString::fromUtf8(v.device()).startsWith(QLatin1String("/dev/mmcblk"))
                && v.rootPath() != QLatin1String("/")) {
                return v.rootPath();
            }
        }
        return {};
    }
    return {};
}

void AliasManager::load()
{
    m_user.clear();
    QSettings s(m_file.isEmpty() ? Paths::configFile(QStringLiteral("aliases.ini")) : m_file,
                QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Aliases"));
    const QStringList keys = s.childKeys();
    for (const QString &k : keys) {
        const QString name = k.toLower();
        const QString path = s.value(k).toString();
        if (isValidName(name) && !isReservedName(name) && (QDir::isAbsolutePath(path) || Location::isRemote(path)))
            m_user.insert(name, Location::clean(path));
    }
    s.endGroup();
}

bool AliasManager::save() const
{
    QSettings s(m_file.isEmpty() ? Paths::configFile(QStringLiteral("aliases.ini")) : m_file,
                QSettings::IniFormat);
    s.remove(QStringLiteral("Aliases"));
    s.beginGroup(QStringLiteral("Aliases"));
    for (auto it = m_user.cbegin(); it != m_user.cend(); ++it)
        s.setValue(it.key(), it.value());
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}

QString AliasManager::aliasPath(const QString &name) const
{
    const QString key = name.toLower();
    if (m_user.contains(key))
        return m_user.value(key);
    if (m_builtIns.contains(key))
        return m_builtIns.value(key);
    return dynamicAlias(key);
}

bool AliasManager::hasAlias(const QString &name) const
{
    return !aliasPath(name).isEmpty();
}

bool AliasManager::isBuiltIn(const QString &name) const
{
    const QString key = name.toLower();
    return m_builtIns.contains(key) || key == QLatin1String("sdcard") || key == QLatin1String("removable");
}

bool AliasManager::isUserAlias(const QString &name) const
{
    return m_user.contains(name.toLower());
}

QStringList AliasManager::names() const
{
    QStringList out;
    QStringList candidates = m_builtIns.keys() + m_user.keys();
    candidates << QStringLiteral("sdcard") << QStringLiteral("removable");
    for (const QString &n : std::as_const(candidates)) {
        if (!out.contains(n) && hasAlias(n))
            out << n;
    }
    out.sort();
    return out;
}

QStringList AliasManager::completions(const QString &prefix) const
{
    QStringList out;
    if (!prefix.startsWith(QLatin1Char('/')) || prefix.indexOf(QLatin1Char('/'), 1) != -1)
        return out;
    const QString p = prefix.mid(1).toLower();
    const QStringList all = names();
    for (const QString &n : all) {
        if (n.startsWith(p))
            out << QLatin1Char('/') + n;
    }
    return out;
}

QString AliasManager::resolve(const QString &input) const
{
    QString s = input.trimmed();
    if (s.isEmpty())
        return s;

    if (s.startsWith(QLatin1String("file:"))) {
        const QUrl url(s);
        if (url.isLocalFile())
            return QDir::cleanPath(url.toLocalFile());
    }

    if (s == QLatin1String("~"))
        return QDir::homePath();
    if (s.startsWith(QLatin1String("~/")))
        return QDir::cleanPath(QDir::homePath() + s.mid(1));

    if (s.startsWith(QLatin1Char('/'))) {
        const int slash = s.indexOf(QLatin1Char('/'), 1);
        const QString first = s.mid(1, slash == -1 ? -1 : slash - 1);
        if (!first.isEmpty() && !isReservedName(first)) {
            const QString target = aliasPath(first);
            if (!target.isEmpty()) {
                const QString rest = slash == -1 ? QString() : s.mid(slash + 1);
                return rest.isEmpty() ? Location::clean(target) : Location::join(target, rest);
            }
        }
        return QDir::cleanPath(s);
    }
    return s;
}

bool AliasManager::isValidName(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$"));
    return re.match(name).hasMatch();
}

bool AliasManager::isReservedName(const QString &name)
{
    const QString key = name.toLower();
    if (kFhsRoots.contains(key))
        return true;
    // Anything that really exists in "/" wins over an alias of the same name.
    return QFileInfo::exists(QLatin1Char('/') + name);
}

bool AliasManager::setUserAlias(const QString &name, const QString &path, QString *error)
{
    auto fail = [&](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    if (!isValidName(name))
        return fail(QStringLiteral("Alias names use letters, digits, '-', '_' and '.', and start with a letter or digit."));
    if (isReservedName(name))
        return fail(QStringLiteral("\"/%1\" is a real folder in the root of the filesystem, so it cannot be an alias.").arg(name));
    const QString resolved = resolve(path);
    if (!QDir::isAbsolutePath(resolved) && !Location::isRemote(resolved))
        return fail(QStringLiteral("The alias target must be an absolute path or a server address."));
    m_user.insert(name.toLower(), Location::clean(resolved));
    return true;
}

bool AliasManager::removeUserAlias(const QString &name)
{
    return m_user.remove(name.toLower()) > 0;
}
