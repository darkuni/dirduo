#include "core/Apps.h"

#include "core/HostRun.h"
#include "core/Paths.h"

#include <QCollator>
#include <QDir>
#include <QElapsedTimer>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSettings>
#include <QTextStream>
#include <QUrl>

#include <algorithm>

namespace Apps {

namespace {

QStringList &dirsOverride()
{
    static QStringList d;
    return d;
}

QList<AppInfo> &cache()
{
    static QList<AppInfo> c;
    return c;
}

bool &loaded()
{
    static bool l = false;
    return l;
}

QSettings settings()
{
    return QSettings(Paths::configFile(QStringLiteral("linopus.ini")), QSettings::IniFormat);
}

// The desktop's own default (mimeapps.list) for a type, if any.
QString desktopDefault(const QString &mime)
{
    QStringList files{QDir::homePath() + QStringLiteral("/.config/mimeapps.list"),
                      QDir::homePath() + QStringLiteral("/.local/share/applications/mimeapps.list")};
    const QString host = HostRun::inFlatpak() ? QStringLiteral("/run/host") : QString();
    files << host + QStringLiteral("/etc/xdg/mimeapps.list") << host + QStringLiteral("/usr/share/applications/mimeapps.list");
    for (const QString &f : files) {
        QSettings s(f, QSettings::IniFormat);
        s.beginGroup(QStringLiteral("Default Applications"));
        const QString v = s.value(mime).toString();
        if (!v.isEmpty())
            return v.section(QLatin1Char(';'), 0, 0).trimmed();
    }
    return {};
}

QString unescapeExecArg(const QString &a)
{
    QString out;
    for (int i = 0; i < a.size(); ++i) {
        if (a.at(i) == QLatin1Char('\\') && i + 1 < a.size()) {
            out += a.at(++i);
            continue;
        }
        out += a.at(i);
    }
    return out;
}

// Splits an Exec line as the desktop entry spec says: quotes group words.
QStringList splitExec(const QString &exec)
{
    QStringList out;
    QString cur;
    bool quoted = false;
    bool any = false;
    for (int i = 0; i < exec.size(); ++i) {
        const QChar c = exec.at(i);
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
            any = true;
            continue;
        }
        if (c == QLatin1Char('\\') && quoted && i + 1 < exec.size()) {
            cur += exec.at(++i);
            continue;
        }
        if (!quoted && c.isSpace()) {
            if (any || !cur.isEmpty())
                out << cur;
            cur.clear();
            any = false;
            continue;
        }
        cur += c;
    }
    if (any || !cur.isEmpty())
        out << cur;
    for (QString &s : out)
        s = unescapeExecArg(s);
    return out;
}

} // namespace

QStringList searchDirs()
{
    if (!dirsOverride().isEmpty())
        return dirsOverride();
    const QString home = QDir::homePath();
    QStringList dirs{home + QStringLiteral("/.local/share/applications"),
                     home + QStringLiteral("/.local/share/flatpak/exports/share/applications"),
                     QStringLiteral("/var/lib/flatpak/exports/share/applications")};
    if (HostRun::inFlatpak()) {
        // The host's system folders are under /run/host in the sandbox.
        dirs << QStringLiteral("/run/host/usr/local/share/applications") << QStringLiteral("/run/host/usr/share/applications");
    } else {
        QStringList data = qEnvironmentVariable("XDG_DATA_DIRS").split(QLatin1Char(':'), Qt::SkipEmptyParts);
        if (data.isEmpty())
            data = {QStringLiteral("/usr/local/share"), QStringLiteral("/usr/share")};
        for (const QString &d : std::as_const(data))
            dirs << d + QStringLiteral("/applications");
    }
    dirs.removeDuplicates();
    return dirs;
}

void setSearchDirsOverride(const QStringList &dirs)
{
    dirsOverride() = dirs;
    reload();
}

void reload()
{
    loaded() = false;
    cache().clear();
}

AppInfo parseDesktopFile(const QString &file, const QString &id)
{
    AppInfo a;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return a;
    QTextStream in(&f);
    bool inEntry = false;
    bool hidden = false;
    QString type;
    QString name;
    while (!in.atEnd()) {
        const QString line = in.readLine().trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (!inEntry || line.startsWith(QLatin1Char('#')))
            continue;
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq < 0)
            continue;
        const QString key = line.left(eq).trimmed();
        const QString value = line.mid(eq + 1).trimmed();
        if (key == QLatin1String("Name"))
            name = value;
        else if (key == QLatin1String("Exec"))
            a.exec = value;
        else if (key == QLatin1String("Icon"))
            a.icon = value;
        else if (key == QLatin1String("Type"))
            type = value;
        else if (key == QLatin1String("MimeType"))
            a.mimeTypes = value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
        else if (key == QLatin1String("Terminal"))
            a.terminal = value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0;
        else if ((key == QLatin1String("NoDisplay") || key == QLatin1String("Hidden"))
                 && value.compare(QLatin1String("true"), Qt::CaseInsensitive) == 0)
            hidden = true;
    }
    if (type != QLatin1String("Application") || hidden || a.exec.isEmpty())
        return AppInfo{};
    a.id = id;
    a.name = name.isEmpty() ? QFileInfo(file).completeBaseName() : name;
    return a;
}

QList<AppInfo> all()
{
    // Read again after a minute: apps installed meanwhile show up.
    static QElapsedTimer age;
    if (loaded() && age.isValid() && age.elapsed() < 60000)
        return cache();
    age.start();
    loaded() = true;
    QList<AppInfo> out;
    QStringList seen; // the first folder with an id wins (user over system)
    for (const QString &dir : searchDirs()) {
        QDirIterator it(dir, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString file = it.next();
            QString id = QDir(dir).relativeFilePath(file);
            id.replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seen.contains(id))
                continue;
            seen << id;
            const AppInfo a = parseDesktopFile(file, id);
            if (a.isValid())
                out << a;
        }
    }
    QCollator coll;
    coll.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(out.begin(), out.end(), [&coll](const AppInfo &a, const AppInfo &b) { return coll.compare(a.name, b.name) < 0; });
    cache() = out;
    return out;
}

AppInfo find(const QString &idOrName)
{
    QString want = idOrName.trimmed();
    for (const AppInfo &a : all()) {
        if (a.id == want || a.id == want + QStringLiteral(".desktop") || a.name.compare(want, Qt::CaseInsensitive) == 0)
            return a;
    }
    return {};
}

QString mimeTypeOf(const QString &path)
{
    return QMimeDatabase().mimeTypeForFile(path).name();
}

QList<AppInfo> forFile(const QString &path)
{
    const QMimeType mt = QMimeDatabase().mimeTypeForFile(path);
    QStringList types{mt.name()};
    types << mt.aliases() << mt.allAncestors();
    QList<AppInfo> out;
    auto addId = [&](const QString &id) {
        if (id.isEmpty())
            return;
        const AppInfo a = find(id);
        if (!a.isValid())
            return;
        for (const AppInfo &e : std::as_const(out)) {
            if (e.id == a.id)
                return;
        }
        out << a;
    };
    addId(defaultFor(mt.name()));
    addId(desktopDefault(mt.name()));
    // Exact type first, then the more general ones (text/plain for scripts...).
    for (const QString &t : std::as_const(types)) {
        for (const AppInfo &a : all()) {
            if (a.mimeTypes.contains(t))
                addId(a.id);
        }
    }
    return out;
}

// "text/plain" would make a group in the settings file: kept as "text|plain".
static QString typeKey(const QString &mimeType)
{
    return QStringLiteral("FileTypes/") + QString(mimeType).replace(QLatin1Char('/'), QLatin1Char('|'));
}

QString defaultFor(const QString &mimeType)
{
    QSettings s = settings();
    return s.value(typeKey(mimeType)).toString();
}

void setDefaultFor(const QString &mimeType, const QString &appId)
{
    QSettings s = settings();
    if (appId.isEmpty())
        s.remove(typeKey(mimeType));
    else
        s.setValue(typeKey(mimeType), appId);
}

QMap<QString, QString> defaults()
{
    QMap<QString, QString> out;
    QSettings s = settings();
    s.beginGroup(QStringLiteral("FileTypes"));
    for (const QString &k : s.childKeys())
        out.insert(QString(k).replace(QLatin1Char('|'), QLatin1Char('/')), s.value(k).toString());
    return out;
}

QStringList commandFor(const AppInfo &app, const QStringList &files)
{
    QStringList out;
    bool usedFiles = false;
    for (const QString &arg : splitExec(app.exec)) {
        if (arg == QLatin1String("%f") || arg == QLatin1String("%u")) {
            if (!files.isEmpty())
                out << (arg == QLatin1String("%u") ? QUrl::fromLocalFile(files.first()).toString() : files.first());
            usedFiles = true;
        } else if (arg == QLatin1String("%F") || arg == QLatin1String("%U")) {
            for (const QString &f : files)
                out << (arg == QLatin1String("%U") ? QUrl::fromLocalFile(f).toString() : f);
            usedFiles = true;
        } else if (arg == QLatin1String("%i")) {
            if (!app.icon.isEmpty())
                out << QStringLiteral("--icon") << app.icon;
        } else if (arg == QLatin1String("%c")) {
            out << app.name;
        } else if (arg.startsWith(QLatin1String("@@"))) {
            // flatpak's exported launchers mark where files go: @@u %U @@
        } else if (arg == QLatin1String("%k") || arg == QLatin1String("%d") || arg == QLatin1String("%D")
                   || arg == QLatin1String("%n") || arg == QLatin1String("%N") || arg == QLatin1String("%v")
                   || arg == QLatin1String("%m")) {
            // deprecated codes: dropped
        } else {
            QString a = arg;
            a.replace(QLatin1String("%%"), QLatin1String("%"));
            out << a;
        }
    }
    // No field code: the files go at the end (as most launchers do).
    if (!usedFiles)
        out << files;
    return out;
}

bool open(const AppInfo &app, const QStringList &files, QString *error)
{
    if (!app.isValid()) {
        if (error)
            *error = QStringLiteral("That application could not be found.");
        return false;
    }
    if (app.terminal) {
        // A terminal program (vim, mc...): in a terminal window.
        QStringList cmd = commandFor(app, files);
        QString line;
        for (const QString &c : cmd) {
            QString q = c;
            q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            line += QLatin1Char('\'') + q + QStringLiteral("' ");
        }
        const QString dir = files.isEmpty() ? QDir::homePath() : QFileInfo(files.first()).absolutePath();
        return HostRun::start(QStringLiteral("sh"),
                              {QStringLiteral("-c"), QStringLiteral("for t in konsole gnome-terminal xfce4-terminal alacritty kitty foot xterm; do "
                                                                    "command -v \"$t\" >/dev/null 2>&1 && exec \"$t\" -e %1; done").arg(line)},
                              dir, error);
    }
    const QStringList cmd = commandFor(app, files);
    if (cmd.isEmpty()) {
        if (error)
            *error = QStringLiteral("%1 has nothing to run.").arg(app.name);
        return false;
    }
    const QString dir = files.isEmpty() ? QDir::homePath() : QFileInfo(files.first()).absolutePath();
    return HostRun::start(cmd.first(), cmd.mid(1), dir, error);
}

bool openWithDefault(const QString &path, QString *error)
{
    const QString id = defaultFor(mimeTypeOf(path));
    if (id.isEmpty())
        return false;
    const AppInfo a = find(id);
    if (!a.isValid())
        return false;
    return open(a, {path}, error);
}

} // namespace Apps
