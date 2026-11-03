#pragma once

#include <QString>

// Programs among the files: what double-clicking should run rather than open
// in another application, the way Dolphin does it. Native programs (ELF, by
// their first bytes, not their name), AppImages and scripts with a #! line.
namespace Executables {

enum class Kind {
    None,     // not a program (shared libraries, documents, everything else)
    Binary,   // an ELF program
    AppImage, // an ELF program named *.AppImage
    Script,   // starts with #!
};

Kind kindOf(const QString &path);
QString kindName(Kind k); // "program", "AppImage", "script"

// Marked executable for this user?
bool isMarkedExecutable(const QString &path);
// Adds execute permission where there is read permission (u, and g / o
// when they may read it), as chmod +x does with the default umask.
bool makeExecutable(const QString &path, QString *error);

// Runs it on the real system (outside the Flatpak when in one), detached,
// in its own folder.
bool run(const QString &path, QString *error);
// In a terminal window that stays open when it ends, so its output can be
// read (scripts like a BIOS checker).
bool runInTerminal(const QString &path, QString *error);

} // namespace Executables
