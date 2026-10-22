#include "core/Trash.h"

#include "core/FileOps.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <QUrl>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

bool ensureTrashLayout(const QString &trash, QString *error)
{
    for (const char *sub : {"files", "info"}) {
        const QString dir = trash + QLatin1Char('/') + QLatin1String(sub);
        if (QFileInfo(dir).isDir())
            continue;
        if (::mkdir(QFile::encodeName(trash).constData(), 0700) != 0 && errno != EEXIST) {
            if (error)
                *error = QStringLiteral("Cannot create %1: %2").arg(trash, QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
        if (::mkdir(QFile::encodeName(dir).constData(), 0700) != 0 && errno != EEXIST) {
            if (error)
                *error = QStringLiteral("Cannot create %1: %2").arg(dir, QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
    }
    return true;
}

// The mount point holding path.
QString topDirOf(const QString &path)
{
    QStorageInfo info(QFileInfo(path).absolutePath());
    return info.isValid() ? info.rootPath() : QString();
}

QString isoNow()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddThh:mm:ss"));
}

} // namespace

namespace Trash {

QString homeTrashDir()
{
    return Paths::userDataHome() + QStringLiteral("/Trash");
}

QString trashDirFor(const QString &path, QString *error)
{
    const QString home = homeTrashDir();
    QDir().mkpath(QFileInfo(home).absolutePath());

    if (FileOps::sameDevice(path, QFileInfo(home).absolutePath())) {
        return ensureTrashLayout(home, error) ? home : QString();
    }

    const QString top = topDirOf(path);
    if (top.isEmpty() || top == QLatin1String("/")) {
        if (error)
            *error = QStringLiteral("This location has no trash. Delete permanently instead.");
        return {};
    }
    const QString uid = QString::number(::getuid());

    // $topdir/.Trash/$uid, only if .Trash is a real sticky directory.
    const QString shared = top + QStringLiteral("/.Trash");
    struct stat st;
    if (::lstat(QFile::encodeName(shared).constData(), &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & S_ISVTX)) {
        const QString mine = shared + QLatin1Char('/') + uid;
        QString ignored;
        if (ensureTrashLayout(mine, &ignored))
            return mine;
    }

    const QString own = top + QStringLiteral("/.Trash-") + uid;
    if (ensureTrashLayout(own, error))
        return own;
    if (error && error->isEmpty())
        *error = QStringLiteral("This device has no usable trash. Delete permanently instead.");
    return {};
}

bool moveToTrash(const QString &path, QString *trashedPath, QString *error)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink()) {
        if (error)
            *error = QStringLiteral("\"%1\" no longer exists.").arg(path);
        return false;
    }
    const QString source = info.absoluteFilePath();
    const QString trash = trashDirFor(source, error);
    if (trash.isEmpty())
        return false;

    // Home trash stores absolute paths; a topdir trash stores paths relative
    // to the mount point so the entry survives the device mounting elsewhere.
    QString infoPath = source;
    if (trash != homeTrashDir()) {
        const QString top = topDirOf(source);
        if (!top.isEmpty() && source.startsWith(top))
            infoPath = source.mid(top.endsWith(QLatin1Char('/')) ? top.size() : top.size() + 1);
    }
    const QByteArray infoBody = QByteArrayLiteral("[Trash Info]\nPath=")
        + QUrl::toPercentEncoding(infoPath, "/")
        + QByteArrayLiteral("\nDeletionDate=") + isoNow().toUtf8() + QByteArrayLiteral("\n");

    // Reserve the name by creating the .trashinfo exclusively, as the spec asks.
    const QString base = info.fileName();
    for (int n = 1; n < 100000; ++n) {
        const QString name = n == 1 ? base : QStringLiteral("%1.%2").arg(base).arg(n);
        const QString infoFile = trash + QStringLiteral("/info/") + name + QStringLiteral(".trashinfo");
        const QString target = trash + QStringLiteral("/files/") + name;
        if (QFileInfo::exists(target) || QFileInfo(target).isSymLink())
            continue;

        const int fd = ::open(QFile::encodeName(infoFile).constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (fd < 0) {
            if (errno == EEXIST)
                continue;
            if (error)
                *error = QStringLiteral("Cannot write to the trash: %1").arg(QString::fromLocal8Bit(std::strerror(errno)));
            return false;
        }
        const ssize_t written = ::write(fd, infoBody.constData(), static_cast<size_t>(infoBody.size()));
        ::close(fd);
        if (written != infoBody.size()) {
            ::unlink(QFile::encodeName(infoFile).constData());
            if (error)
                *error = QStringLiteral("Cannot write to the trash.");
            return false;
        }

        if (::rename(QFile::encodeName(source).constData(), QFile::encodeName(target).constData()) != 0) {
            const int err = errno;
            ::unlink(QFile::encodeName(infoFile).constData());
            if (error)
                *error = QStringLiteral("Cannot move \"%1\" to the trash: %2").arg(base, QString::fromLocal8Bit(std::strerror(err)));
            return false;
        }
        if (trashedPath)
            *trashedPath = target;
        return true;
    }
    if (error)
        *error = QStringLiteral("The trash already holds too many items named \"%1\".").arg(base);
    return false;
}

} // namespace Trash
