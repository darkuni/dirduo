#include "core/Attributes.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Attributes {

namespace {

QByteArray enc(const QString &p)
{
    return QFile::encodeName(p);
}

QString errnoText()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

struct timespec toSpec(const QDateTime &t)
{
    struct timespec ts;
    const qint64 ms = t.toMSecsSinceEpoch();
    ts.tv_sec = time_t(ms / 1000);
    ts.tv_nsec = long((ms % 1000) * 1000000);
    if (ts.tv_nsec < 0) {
        ts.tv_sec -= 1;
        ts.tv_nsec += 1000000000;
    }
    return ts;
}

} // namespace

Info read(const QString &path)
{
    Info i;
    const QFileInfo fi(path);
    struct stat st;
    if (::lstat(enc(path).constData(), &st) != 0)
        return i;
    i.exists = true;
    i.isLink = S_ISLNK(st.st_mode);
    if (i.isLink) {
        i.linkTarget = fi.symLinkTarget();
        char buf[4096];
        const ssize_t n = ::readlink(enc(path).constData(), buf, sizeof(buf) - 1);
        if (n >= 0)
            i.linkTarget = QFile::decodeName(QByteArray(buf, int(n)));
        struct stat target;
        i.brokenLink = ::stat(enc(path).constData(), &target) != 0;
        if (!i.brokenLink)
            st = target; // describe what it points to
    }
    i.isDir = S_ISDIR(st.st_mode);
    i.mode = int(st.st_mode & 07777);
    i.size = S_ISREG(st.st_mode) ? qint64(st.st_size) : 0;
    i.owner = fi.owner();
    i.group = fi.group();
    if (i.owner.isEmpty())
        i.owner = QString::number(st.st_uid);
    if (i.group.isEmpty())
        i.group = QString::number(st.st_gid);
    i.modified = QDateTime::fromMSecsSinceEpoch(qint64(st.st_mtim.tv_sec) * 1000 + st.st_mtim.tv_nsec / 1000000);
    i.accessed = QDateTime::fromMSecsSinceEpoch(qint64(st.st_atim.tv_sec) * 1000 + st.st_atim.tv_nsec / 1000000);
    i.created = fi.birthTime();
    return i;
}

int ModeChange::apply(int mode, bool isDir) const
{
    int m = exact ? *exact : mode;
    int a = add;
    if (isDir && (a & 0444)) {
        // Readable folders need x to be opened: give x where r is given.
        a |= (a & 0444) >> 2;
    }
    m = (m | a) & ~remove;
    return m & 07777;
}

bool parseMode(const QString &raw, ModeChange &out)
{
    out = ModeChange();
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return false;
    // Octal.
    static const QRegularExpression octalRe(QStringLiteral("^0?[0-7]{3,4}$"));
    if (octalRe.match(text).hasMatch()) {
        bool ok = false;
        out.exact = text.toInt(&ok, 8);
        return ok;
    }
    // rwxr-xr-x (with or without the leading type letter).
    QString sym = text;
    if (sym.size() == 10)
        sym = sym.mid(1);
    static const QRegularExpression symRe(QStringLiteral("^[r-][w-][xsS-][r-][w-][xsS-][r-][w-][xtT-]$"));
    if (symRe.match(sym).hasMatch()) {
        int m = 0;
        const int bits[9] = {0400, 0200, 0100, 040, 020, 010, 04, 02, 01};
        for (int k = 0; k < 9; ++k) {
            const QChar c = sym.at(k);
            if (c != QLatin1Char('-') && c != QLatin1Char('S') && c != QLatin1Char('T'))
                m |= bits[k];
        }
        if (sym.at(2) == QLatin1Char('s') || sym.at(2) == QLatin1Char('S'))
            m |= 04000;
        if (sym.at(5) == QLatin1Char('s') || sym.at(5) == QLatin1Char('S'))
            m |= 02000;
        if (sym.at(8) == QLatin1Char('t') || sym.at(8) == QLatin1Char('T'))
            m |= 01000;
        out.exact = m;
        return true;
    }
    // u+x,go-w style.
    static const QRegularExpression clauseRe(QStringLiteral("^([ugoa]*)([+-=])([rwx]+)$"));
    for (const QString &clause : text.split(QLatin1Char(','))) {
        const QRegularExpressionMatch m = clauseRe.match(clause.trimmed());
        if (!m.hasMatch())
            return false;
        QString who = m.captured(1);
        if (who.isEmpty() || who.contains(QLatin1Char('a')))
            who = QStringLiteral("ugo");
        int bits = 0;
        for (QChar w : who) {
            const int shift = w == QLatin1Char('u') ? 6 : (w == QLatin1Char('g') ? 3 : 0);
            for (QChar p : m.captured(3)) {
                const int b = p == QLatin1Char('r') ? 4 : (p == QLatin1Char('w') ? 2 : 1);
                bits |= b << shift;
            }
        }
        const QString op = m.captured(2);
        if (op == QLatin1String("+")) {
            out.add |= bits;
            out.remove &= ~bits;
        } else if (op == QLatin1String("-")) {
            out.remove |= bits;
            out.add &= ~bits;
        } else { // =: those classes get exactly these bits
            int mask = 0;
            for (QChar w : who)
                mask |= 07 << (w == QLatin1Char('u') ? 6 : (w == QLatin1Char('g') ? 3 : 0));
            out.remove |= mask & ~bits;
            out.add |= bits;
        }
    }
    return true;
}

QString modeString(int mode, bool isDir)
{
    QString s = isDir ? QStringLiteral("d") : QStringLiteral("-");
    const char *letters = "rwxrwxrwx";
    for (int k = 0; k < 9; ++k)
        s += (mode & (0400 >> k)) ? QLatin1Char(letters[k]) : QLatin1Char('-');
    return s;
}

QString octal(int mode)
{
    return QString::number(mode & 07777, 8).rightJustified(3, QLatin1Char('0'));
}

QDateTime parseDate(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.compare(QLatin1String("now"), Qt::CaseInsensitive) == 0)
        return QDateTime::currentDateTime();
    for (const char *f : {"yyyy-MM-dd HH:mm:ss", "yyyy-MM-dd HH:mm", "yyyy-MM-ddTHH:mm:ss", "yyyy-MM-ddTHH:mm", "yyyy-MM-dd"}) {
        const QDateTime t = QDateTime::fromString(text, QLatin1String(f));
        if (t.isValid())
            return t;
    }
    return {};
}

int apply(const QStringList &paths, const Change &change, QStringList *errors)
{
    int changed = 0;
    auto one = [&](const QString &path) {
        struct stat st;
        if (::lstat(enc(path).constData(), &st) != 0) {
            if (errors)
                *errors << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), errnoText());
            return;
        }
        bool did = false;
        if (!change.mode.isEmpty() && !S_ISLNK(st.st_mode)) {
            const int now = int(st.st_mode & 07777);
            const int want = change.mode.apply(now, S_ISDIR(st.st_mode));
            if (want != now) {
                if (::chmod(enc(path).constData(), mode_t(want)) != 0) {
                    if (errors)
                        *errors << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), errnoText());
                } else {
                    did = true;
                }
            }
        }
        if (change.modified || change.accessed) {
            struct timespec times[2];
            times[0].tv_nsec = UTIME_OMIT;
            times[1].tv_nsec = UTIME_OMIT;
            times[0].tv_sec = times[1].tv_sec = 0;
            if (change.accessed)
                times[0] = toSpec(*change.accessed);
            if (change.modified)
                times[1] = toSpec(*change.modified);
            // A link's own dates: the link, not what it points to.
            if (::utimensat(AT_FDCWD, enc(path).constData(), times, AT_SYMLINK_NOFOLLOW) != 0) {
                if (errors)
                    *errors << QStringLiteral("%1: %2").arg(QFileInfo(path).fileName(), errnoText());
            } else {
                did = true;
            }
        }
        if (did)
            ++changed;
    };
    for (const QString &raw : paths) {
        const QString path = QDir::cleanPath(raw);
        // Inside first, then the folder (its date would change otherwise... it
        // does not for chmod, but keep dates set last).
        const QFileInfo fi(path);
        if (change.recurse && fi.isDir() && !fi.isSymLink()) {
            QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext())
                one(it.next());
        }
        one(path);
    }
    return changed;
}

bool makeLink(const QString &target, const QString &linkPath, bool relative, QString *error)
{
    QString to = QDir::cleanPath(target);
    if (relative)
        to = QDir(QFileInfo(linkPath).absolutePath()).relativeFilePath(to);
    if (QFileInfo::exists(linkPath) || QFileInfo(linkPath).isSymLink()) {
        if (error)
            *error = QStringLiteral("\"%1\" is already there.").arg(QFileInfo(linkPath).fileName());
        return false;
    }
    if (::symlink(enc(to).constData(), enc(linkPath).constData()) != 0) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(QFileInfo(linkPath).fileName(), errnoText());
        return false;
    }
    return true;
}

} // namespace Attributes
