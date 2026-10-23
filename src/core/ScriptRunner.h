#pragma once

#include <QObject>
#include <QProcess>
#include <QStringList>

// Runs a button's shell script on the real system (outside the Flatpak
// sandbox when there is one). A button function is a script when its first
// line is a shebang (#!/bin/bash, #!/usr/bin/env python3...). The selected
// items are its arguments ("$@"), it starts in the source folder, and it gets:
//   LINOPUS_SOURCE   the source folder
//   LINOPUS_DEST     the destination folder (empty without one)
//   LINOPUS_FOCUSED  the item with the keyboard focus
// Lines starting with "# linopus:" choose how it runs:
//   # linopus: terminal   in a terminal window (Konsole), kept open at the end
//   # linopus: output     always show what it printed (otherwise only on failure)
//   # linopus: norefresh  leave the listings alone afterwards
struct ScriptOptions
{
    bool terminal = false;
    bool showOutput = false;
    bool refresh = true;
};

struct ScriptResult
{
    int exitCode = 0;
    bool crashed = false;    // killed, or could not start
    QString output;          // stdout and stderr together
};

class ScriptRunner : public QObject
{
    Q_OBJECT
public:
    explicit ScriptRunner(QObject *parent = nullptr);

    static bool isScript(const QString &function);
    static ScriptOptions optionsOf(const QString &script);

    // Starts it and returns; finished() follows (at once for terminal mode,
    // which only opens the window). False with error when it cannot start.
    bool start(const QString &script, const QStringList &args, const QString &dir, const QString &source,
               const QString &dest, const QString &focused, QString *error);
    // Waits for it (tests).
    bool waitForFinished(int msecs = 30000);

    // Where scripts are written before running: the host must see the file.
    static QString scriptDir();

Q_SIGNALS:
    void finished(const ScriptResult &result, const ScriptOptions &options);

private:
    QProcess m_process;
    ScriptOptions m_options;
    QString m_file;
};
