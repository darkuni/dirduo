#include "core/HostRun.h"

#include <QFile>
#include <QProcess>

namespace HostRun {

namespace {

std::function<bool(const QStringList &, const QString &)> &starter()
{
    static std::function<bool(const QStringList &, const QString &)> s;
    return s;
}

QString shellQuote(const QString &s)
{
    QString q = s;
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    return QLatin1Char('\'') + q + QLatin1Char('\'');
}

} // namespace

void setStarter(std::function<bool(const QStringList &, const QString &)> s)
{
    starter() = std::move(s);
}

bool inFlatpak()
{
    return QFile::exists(QStringLiteral("/.flatpak-info"));
}

QStringList commandLine(const QString &program, const QStringList &args, const QString &dir)
{
    QStringList cmd;
    if (inFlatpak()) {
        cmd << QStringLiteral("flatpak-spawn") << QStringLiteral("--host");
        if (!dir.isEmpty())
            cmd << QStringLiteral("--directory=") + dir;
    }
    cmd << program << args;
    return cmd;
}

bool start(const QString &program, const QStringList &args, const QString &dir, QString *error)
{
    const QStringList cmd = commandLine(program, args, dir);
    if (starter())
        return starter()(cmd, dir);
    qint64 pid = 0;
    if (!QProcess::startDetached(cmd.first(), cmd.mid(1), dir, &pid)) {
        if (error)
            *error = QStringLiteral("Could not start %1.").arg(program);
        return false;
    }
    return true;
}

QStringList terminalCommand(const QString &dir, const QString &program)
{
    // One shell line on the host: the first terminal that is installed.
    // Konsole is told the folder; the others start in the current one.
    QStringList candidates;
    if (!program.isEmpty())
        candidates << program;
    for (const char *t : {"konsole", "gnome-terminal", "kgx", "xfce4-terminal", "tilix", "alacritty", "kitty", "foot", "xterm"}) {
        const QString name = QString::fromLatin1(t);
        if (!candidates.contains(name))
            candidates << name;
    }
    QString script;
    for (const QString &t : std::as_const(candidates)) {
        const QString exec = t == QLatin1String("konsole") ? QStringLiteral("exec konsole --workdir %1").arg(shellQuote(dir))
                                                           : QStringLiteral("exec %1").arg(shellQuote(t));
        script += QStringLiteral("command -v %1 >/dev/null 2>&1 && %2; ").arg(shellQuote(t), exec);
    }
    script += QStringLiteral("exit 127");
    return {QStringLiteral("sh"), QStringLiteral("-c"), script};
}

bool openTerminal(const QString &dir, const QString &program, QString *error)
{
    const QStringList cmd = terminalCommand(dir, program);
    if (!start(cmd.first(), cmd.mid(1), dir, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("No terminal could be started.");
        return false;
    }
    return true;
}

} // namespace HostRun
