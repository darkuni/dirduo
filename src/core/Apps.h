#pragma once

#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

// The applications installed on the system (their .desktop files, read from
// the host when Linopus runs as a Flatpak), which open which file types, and
// Linopus's own choices: Open With, and a default per type that wins over the
// desktop's.
struct AppInfo
{
    QString id;       // "org.kde.kate.desktop"
    QString name;     // "Kate"
    QString exec;     // Exec= line, field codes and all
    QString icon;     // theme name or absolute path
    QStringList mimeTypes;
    bool terminal = false;
    bool isValid() const { return !id.isEmpty() && !exec.isEmpty(); }
};

namespace Apps {

// Where .desktop files are looked for (host paths seen from the sandbox).
QStringList searchDirs();
// Tests: look only here (empty: back to the real places).
void setSearchDirsOverride(const QStringList &dirs);
void reload(); // forget what was read

QList<AppInfo> all();               // visible applications, by name
AppInfo find(const QString &idOrName);
// Applications for a file: Linopus's default first, then the desktop's
// default, then the others that say they open its type (or a parent type).
QList<AppInfo> forFile(const QString &path);
QString mimeTypeOf(const QString &path);

// Linopus's own default per file type (kept in linopus.ini).
QString defaultFor(const QString &mimeType);
void setDefaultFor(const QString &mimeType, const QString &appId); // empty: remove
QMap<QString, QString> defaults();

// The command line for opening files with an app (field codes filled in).
QStringList commandFor(const AppInfo &app, const QStringList &files);
// Opens files with it on the real system.
bool open(const AppInfo &app, const QStringList &files, QString *error);
// Opens a file with Linopus's default for its type; false when there is none
// (the caller then asks the desktop).
bool openWithDefault(const QString &path, QString *error);

AppInfo parseDesktopFile(const QString &file, const QString &id);

} // namespace Apps
