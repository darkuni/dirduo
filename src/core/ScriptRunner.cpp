#include "core/ScriptRunner.h"

#include "core/HostRun.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QRandomGenerator>
#include <QRegularExpression>

ScriptRunner::ScriptRunner(QObject *parent)
    : QObject(parent)
{
    m_process.setProcessChannelMode(QProcess::MergedChannels);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        ScriptResult r;
        r.exitCode = code;
        r.crashed = status == QProcess::CrashExit;
        r.output = QString::fromLocal8Bit(m_process.readAll());
        QFile::remove(m_file);
        Q_EMIT finished(r, m_options);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart)
            return;
        ScriptResult r;
        r.crashed = true;
        r.exitCode = -1;
        r.output = QStringLiteral("The script could not be started: %1").arg(m_process.errorString());
        QFile::remove(m_file);
        Q_EMIT finished(r, m_options);
    });
}

bool ScriptRunner::isScript(const QString &function)
{
    return function.trimmed().startsWith(QLatin1String("#!"));
}

ScriptOptions ScriptRunner::optionsOf(const QString &script)
{
    ScriptOptions o;
    static const QRegularExpression re(QStringLiteral("^\\s*#\\s*linopus:\\s*(\\w+)"), QRegularExpression::CaseInsensitiveOption);
    for (const QString &line : script.split(QLatin1Char('\n'))) {
        const QRegularExpressionMatch m = re.match(line);
        if (!m.hasMatch())
            continue;
        const QString w = m.captured(1).toLower();
        if (w == QLatin1String("terminal"))
            o.terminal = true;
        else if (w == QLatin1String("output"))
            o.showOutput = true;
        else if (w == QLatin1String("norefresh"))
            o.refresh = false;
    }
    return o;
}

QString ScriptRunner::scriptDir()
{
    // The cache folder is a real folder on the host too (inside the Flatpak
    // it is ~/.var/app/<id>/cache), so the host can read the file.
    return Paths::userCacheHome() + QStringLiteral("/linopus/scripts");
}

bool ScriptRunner::start(const QString &script, const QStringList &args, const QString &dir, const QString &source,
                         const QString &dest, const QString &focused, QString *error)
{
    m_options = optionsOf(script);
    QDir().mkpath(scriptDir());
    m_file = scriptDir() + QStringLiteral("/button-%1.sh").arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    {
        QFile f(m_file);
        if (!f.open(QIODevice::WriteOnly)) {
            if (error)
                *error = QStringLiteral("Could not write the script to %1.").arg(m_file);
            return false;
        }
        QString text = script.trimmed();
        f.write(text.toUtf8() + '\n');
    }
    QFile::setPermissions(m_file, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QStringList env = {QStringLiteral("LINOPUS_SOURCE=") + source, QStringLiteral("LINOPUS_DEST=") + dest,
                             QStringLiteral("LINOPUS_FOCUSED=") + focused};

    if (m_options.terminal) {
        // In a terminal: run it, then wait for Enter so the output stays.
        QString line = QStringLiteral("\"$0\" \"$@\"; s=$?; echo; read -rp \"Finished (exit $s). Press Enter to close.\" _");
        QStringList inner{QStringLiteral("bash"), QStringLiteral("-c"), line, m_file};
        inner << args;
        QString quotedInner;
        for (const QString &a : inner) {
            QString q = a;
            q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            quotedInner += QLatin1Char('\'') + q + QStringLiteral("' ");
        }
        // Konsole runs the line with -e; other terminals take it after -e too.
        QString envPrefix;
        for (const QString &e : env) {
            QString q = e;
            q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
            envPrefix += QLatin1Char('\'') + q + QStringLiteral("' ");
        }
        const QString sh = QStringLiteral("for t in konsole gnome-terminal xfce4-terminal alacritty kitty foot xterm; do "
                                          "command -v \"$t\" >/dev/null 2>&1 && exec \"$t\" -e env %1%2; done; exit 127")
                               .arg(envPrefix, quotedInner);
        const bool ok = HostRun::start(QStringLiteral("sh"), {QStringLiteral("-c"), sh}, dir, error);
        ScriptResult r;
        r.exitCode = ok ? 0 : -1;
        r.crashed = !ok;
        // The file stays for the terminal to read; scripts are small and the
        // folder is cleared at the next start.
        Q_EMIT finished(r, m_options);
        return ok;
    }

    QStringList cmd = HostRun::commandLine(m_file, args, dir);
    QStringList withEnv;
    if (HostRun::inFlatpak()) {
        // flatpak-spawn --host [--directory=] --env=... program args
        withEnv << cmd.mid(0, 2);
        int at = 2;
        if (cmd.value(2).startsWith(QLatin1String("--directory=")))
            withEnv << cmd.value(at++);
        for (const QString &e : env)
            withEnv << QStringLiteral("--env=") + e;
        withEnv << cmd.mid(at);
    } else {
        withEnv = cmd;
        QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
        for (const QString &e : env)
            pe.insert(e.section(QLatin1Char('='), 0, 0), e.section(QLatin1Char('='), 1));
        m_process.setProcessEnvironment(pe);
    }
    m_process.setWorkingDirectory(dir);
    m_process.start(withEnv.first(), withEnv.mid(1));
    return true;
}

bool ScriptRunner::waitForFinished(int msecs)
{
    return m_process.waitForFinished(msecs);
}
