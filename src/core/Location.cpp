#include "core/Location.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

const QRegularExpression &schemeRe()
{
    // RFC 3986 scheme, followed by ':'. A single letter is not treated as a
    // scheme so a Windows-style "C:" typed by habit is not mistaken for one.
    static const QRegularExpression re(QStringLiteral("^([A-Za-z][A-Za-z0-9+.-]+):"));
    return re;
}

QString schemeOf(const QString &loc)
{
    const auto m = schemeRe().match(loc);
    return m.hasMatch() ? m.captured(1).toLower() : QString();
}

QString cleanUrlPath(QString path)
{
    if (path.isEmpty())
        return QStringLiteral("/");
    path = QDir::cleanPath(path);
    if (!path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));
    return path;
}

} // namespace

namespace Location {

QStringList archiveSchemes()
{
    return {QStringLiteral("zip"), QStringLiteral("tar"), QStringLiteral("ar"), QStringLiteral("sevenz"),
            QStringLiteral("7z"), QStringLiteral("archive"), QStringLiteral("krarc"), QStringLiteral("iso")};
}

bool isRemote(const QString &loc)
{
    const QString scheme = schemeOf(loc);
    return !scheme.isEmpty() && scheme != QLatin1String("file");
}

bool isLocal(const QString &loc)
{
    return !isRemote(loc);
}

bool isArchive(const QString &loc)
{
    return archiveSchemes().contains(schemeOf(loc));
}

QUrl toUrl(const QString &loc)
{
    if (isRemote(loc))
        return QUrl(loc);
    if (loc.startsWith(QLatin1String("file:")))
        return QUrl(loc);
    return QUrl::fromLocalFile(loc);
}

QString fromUrl(const QUrl &url)
{
    if (url.isLocalFile())
        return QDir::cleanPath(url.toLocalFile());
    return clean(url.toString(QUrl::FullyEncoded));
}

QList<QUrl> toUrls(const QStringList &locs)
{
    QList<QUrl> out;
    out.reserve(locs.size());
    for (const QString &l : locs)
        out << toUrl(l);
    return out;
}

QString clean(const QString &loc)
{
    if (loc.isEmpty())
        return loc;
    if (!isRemote(loc)) {
        if (loc.startsWith(QLatin1String("file:")))
            return QDir::cleanPath(QUrl(loc).toLocalFile());
        return QDir::cleanPath(loc);
    }
    QUrl url(loc);
    if (!url.isValid())
        return loc;
    url.setPath(cleanUrlPath(url.path()));
    url.setQuery(QString());
    url.setFragment(QString());
    QString s = url.toString(QUrl::FullyEncoded);
    // "zip:/a/b" style URLs have no authority; keep them single-slashed.
    return s;
}

bool isRoot(const QString &loc)
{
    if (!isRemote(loc))
        return QDir::cleanPath(loc) == QLatin1String("/");
    if (isArchive(loc))
        return false; // the way out of an archive is its folder
    return QUrl(loc).path() == QLatin1String("/") || QUrl(loc).path().isEmpty();
}

QString archiveFile(const QString &loc)
{
    if (!isArchive(loc))
        return {};
    QString path = cleanUrlPath(QUrl(loc).path());
    // Walk up until the path is the archive file on disk.
    while (path != QLatin1String("/") && !path.isEmpty()) {
        if (QFileInfo(path).isFile())
            return path;
        path = QFileInfo(path).absolutePath();
    }
    return {};
}

QString parent(const QString &loc)
{
    if (!isRemote(loc))
        return QFileInfo(QDir::cleanPath(loc)).absolutePath();
    QUrl url(loc);
    const QString path = cleanUrlPath(url.path());
    if (isArchive(loc)) {
        const QString file = archiveFile(loc);
        if (!file.isEmpty() && path == file)
            return QFileInfo(file).absolutePath(); // out of the archive, back to its folder
    }
    if (path == QLatin1String("/"))
        return clean(loc);
    url.setPath(QFileInfo(path).absolutePath());
    return clean(url.toString(QUrl::FullyEncoded));
}

QString fileName(const QString &loc)
{
    if (!isRemote(loc))
        return QFileInfo(QDir::cleanPath(loc)).fileName();
    const QString path = cleanUrlPath(QUrl(loc).path());
    return path == QLatin1String("/") ? QString() : path.mid(path.lastIndexOf(QLatin1Char('/')) + 1);
}

QString join(const QString &dir, const QString &name)
{
    if (!isRemote(dir))
        return QDir::cleanPath(dir + QLatin1Char('/') + name);
    QUrl url(dir);
    QString path = cleanUrlPath(url.path());
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    url.setPath(path + name);
    return clean(url.toString(QUrl::FullyEncoded));
}

bool isSameOrInside(const QString &child, const QString &parentLoc)
{
    const QString c = clean(child);
    const QString p = clean(parentLoc);
    if (c == p)
        return true;
    if (isRemote(c) != isRemote(p))
        return false;
    if (!isRemote(c)) {
        if (p == QLatin1String("/"))
            return c.startsWith(QLatin1Char('/'));
        return c.startsWith(p + QLatin1Char('/'));
    }
    const QUrl cu(c);
    const QUrl pu(p);
    if (cu.scheme() != pu.scheme() || cu.authority() != pu.authority())
        return false;
    const QString pp = cleanUrlPath(pu.path());
    const QString cp = cleanUrlPath(cu.path());
    return pp == QLatin1String("/") || cp.startsWith(pp + QLatin1Char('/'));
}

QString title(const QString &loc)
{
    if (!isRemote(loc)) {
        const QString p = QDir::cleanPath(loc);
        if (p == QLatin1String("/"))
            return QStringLiteral("/");
        if (p == QDir::homePath())
            return QStringLiteral("Home");
        return QFileInfo(p).fileName();
    }
    const QString name = fileName(loc);
    if (!name.isEmpty())
        return name;
    const QUrl url(loc);
    return url.host().isEmpty() ? url.scheme() + QLatin1Char(':') : url.host();
}

QString display(const QString &loc)
{
    if (!isRemote(loc))
        return loc;
    return QUrl(loc).toDisplayString(QUrl::RemovePassword | QUrl::PreferLocalFile);
}

QString serverKey(const QString &loc)
{
    if (!isRemote(loc))
        return {};
    const QUrl url(loc);
    if (isArchive(loc))
        return QStringLiteral("archive:") + archiveFile(loc);
    QString key = url.scheme() + QStringLiteral("://");
    if (!url.userName().isEmpty())
        key += url.userName() + QLatin1Char('@');
    key += url.host();
    if (url.port() != -1)
        key += QLatin1Char(':') + QString::number(url.port());
    return key;
}

} // namespace Location
