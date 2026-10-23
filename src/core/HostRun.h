#pragma once

#include <QString>
#include <QStringList>

#include <functional>

// Starts programs on the real system, outside the Flatpak sandbox when
// Linopus runs in one (through flatpak-spawn --host), detached.
namespace HostRun {

bool inFlatpak();

// The full command line that will run: program and args, wrapped for the
// host when sandboxed, starting in dir.
QStringList commandLine(const QString &program, const QStringList &args, const QString &dir);

// Starts it and returns at once; false with error when it could not start.
bool start(const QString &program, const QStringList &args, const QString &dir, QString *error);

// A terminal window in dir: the given program, or Konsole, falling back to
// the other common terminals when it is not installed.
bool openTerminal(const QString &dir, const QString &program, QString *error);
// What openTerminal runs (before wrapping for the host).
QStringList terminalCommand(const QString &dir, const QString &program);

// Tests: called instead of starting anything; return true for success.
void setStarter(std::function<bool(const QStringList &commandLine, const QString &dir)> starter);

} // namespace HostRun
