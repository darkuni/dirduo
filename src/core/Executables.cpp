#include "core/Executables.h"

#include "core/HostRun.h"

#include <QFile>
#include <QFileInfo>
#include <QtEndian>

namespace Executables {

namespace {

constexpr quint32 kPtInterp = 3;
constexpr quint16 kEtExec = 2;
constexpr quint16 kEtDyn = 3;

template<typename T>
T readAt(const QByteArray &b, qsizetype at, bool little)
{
    if (at < 0 || at + qsizetype(sizeof(T)) > b.size())
        return 0;
    T v;
    memcpy(&v, b.constData() + at, sizeof(T));
    return little ? qFromLittleEndian(v) : qFromBigEndian(v);
}

// ET_EXEC is a program. ET_DYN is a program when it asks for a loader
// (PT_INTERP, as every PIE program does); shared libraries do not.
bool elfIsProgram(QFile &f)
{
    const QByteArray head = f.read(64);
    if (head.size() < 52)
        return false;
    const bool is64 = head.at(4) == 2;
    const bool little = head.at(5) == 1;
    const quint16 type = readAt<quint16>(head, 16, little);
    if (type == kEtExec)
        return true;
    if (type != kEtDyn)
        return false;
    const quint64 phoff = is64 ? readAt<quint64>(head, 32, little) : readAt<quint32>(head, 28, little);
    const quint16 phentsize = readAt<quint16>(head, is64 ? 54 : 42, little);
    const quint16 phnum = readAt<quint16>(head, is64 ? 56 : 44, little);
    if (phentsize == 0 || phnum == 0 || phnum > 512 || phoff == 0)
        return false;
    if (!f.seek(qint64(phoff)))
        return false;
    const QByteArray ph = f.read(qint64(phentsize) * phnum);
    for (int i = 0; i < phnum; ++i) {
        if (readAt<quint32>(ph, qsizetype(i) * phentsize, little) == kPtInterp)
            return true;
    }
    return false;
}

} // namespace

Kind kindOf(const QString &path)
{
    const QFileInfo fi(path);
    if (!fi.isFile())
        return Kind::None;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return Kind::None;
    const QByteArray magic = f.peek(4);
    if (magic.startsWith("#!"))
        return Kind::Script;
    if (magic == QByteArray("\x7f" "ELF", 4)) {
        if (!elfIsProgram(f))
            return Kind::None;
        return fi.fileName().endsWith(QLatin1String(".appimage"), Qt::CaseInsensitive) ? Kind::AppImage : Kind::Binary;
    }
    return Kind::None;
}

QString kindName(Kind k)
{
    switch (k) {
    case Kind::Binary:
        return QStringLiteral("program");
    case Kind::AppImage:
        return QStringLiteral("AppImage");
    case Kind::Script:
        return QStringLiteral("script");
    case Kind::None:
        break;
    }
    return QString();
}

bool isMarkedExecutable(const QString &path)
{
    const QFileInfo fi(path);
    return fi.isFile() && fi.isExecutable();
}

bool makeExecutable(const QString &path, QString *error)
{
    QFile f(path);
    QFileDevice::Permissions p = f.permissions();
    p |= QFileDevice::ExeOwner | QFileDevice::ExeUser;
    if (p & QFileDevice::ReadGroup)
        p |= QFileDevice::ExeGroup;
    if (p & QFileDevice::ReadOther)
        p |= QFileDevice::ExeOther;
    if (!f.setPermissions(p)) {
        if (error)
            *error = QStringLiteral("Could not make \"%1\" executable: %2").arg(QFileInfo(path).fileName(), f.errorString());
        return false;
    }
    return true;
}

bool run(const QString &path, QString *error)
{
    const QFileInfo fi(path);
    return HostRun::start(fi.absoluteFilePath(), {}, fi.absolutePath(), error);
}

bool runInTerminal(const QString &path, QString *error)
{
    const QFileInfo fi(path);
    const QString dir = fi.absolutePath();
    QString q = fi.absoluteFilePath();
    q.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    q = QLatin1Char('\'') + q + QLatin1Char('\'');
    QString d = dir;
    d.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    d = QLatin1Char('\'') + d + QLatin1Char('\'');
    // Konsole keeps the window open (--hold); the others get a pause at the
    // end, so what it printed can be read.
    const QString wait = QStringLiteral("%1; echo; printf 'Press Enter to close.'; read _").arg(q);
    QString waitQ = wait;
    waitQ.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    const QString script =
        QStringLiteral("command -v konsole >/dev/null 2>&1 && exec konsole --hold --workdir %1 -e %2; "
                       "command -v gnome-terminal >/dev/null 2>&1 && exec gnome-terminal --working-directory=%1 -- sh -c '%3'; "
                       "for t in kgx xfce4-terminal tilix alacritty kitty foot xterm; do "
                       "command -v \"$t\" >/dev/null 2>&1 && exec \"$t\" -e sh -c '%3'; done; exit 127")
            .arg(d, q, waitQ);
    if (!HostRun::start(QStringLiteral("sh"), {QStringLiteral("-c"), script}, dir, error)) {
        if (error && error->isEmpty())
            *error = QStringLiteral("No terminal could be started.");
        return false;
    }
    return true;
}

} // namespace Executables
