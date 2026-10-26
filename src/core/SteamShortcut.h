#pragma once

#include <QString>
#include <QStringList>

// Adding a program to Steam as a non-Steam game, as Dolphin's "Add to Steam"
// on SteamOS does. On SteamOS it runs steamos-add-to-steam; elsewhere it
// opens Steam's steam://addnonsteamgame/ link (Steam must be running).
namespace SteamShortcut {

// Programs Steam can start: Windows programs (.exe, .bat, .msi, run through
// Proton), AppImages, shell scripts, .desktop launchers, or any file marked
// executable.
bool canAdd(const QString &path);
// steam://addnonsteamgame/<the path, percent-encoded>
QString addUrl(const QString &path);
// The shell script that does it on the real system, run as
// sh -c <script> sh <path> <url>.
QString hostScript();
bool add(const QString &path, QString *error);

} // namespace SteamShortcut
