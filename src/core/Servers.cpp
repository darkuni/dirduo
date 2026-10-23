#include "core/Servers.h"

#include "core/Paths.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUrl>

Servers *Servers::instance()
{
    static Servers s;
    return &s;
}

QString Servers::file() const
{
    return Paths::configFile(QStringLiteral("servers.json"));
}

QList<Server> Servers::items() const
{
    QList<Server> out;
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly))
        return out;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Server s{o.value(QStringLiteral("name")).toString(), o.value(QStringLiteral("url")).toString()};
        if (!s.name.isEmpty() && !s.url.isEmpty())
            out << s;
    }
    return out;
}

void Servers::setItems(const QList<Server> &items)
{
    QJsonArray arr;
    for (const Server &s : items)
        arr.append(QJsonObject{{QStringLiteral("name"), s.name}, {QStringLiteral("url"), s.url}});
    QSaveFile f(file());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(arr).toJson());
        f.commit();
    }
    Q_EMIT changed();
}

bool Servers::isServerUrl(const QString &url)
{
    const QUrl u(url);
    static const QStringList schemes = {QStringLiteral("sftp"), QStringLiteral("ftp"), QStringLiteral("ftps"), QStringLiteral("smb"),
                                        QStringLiteral("webdav"), QStringLiteral("webdavs"), QStringLiteral("fish"), QStringLiteral("nfs")};
    return u.isValid() && schemes.contains(u.scheme().toLower()) && (!u.host().isEmpty() || u.scheme() == QLatin1String("smb"));
}

bool Servers::add(const Server &server, QString *error)
{
    const QString name = server.name.trimmed();
    if (name.isEmpty() || name.contains(QLatin1Char('\n'))) {
        if (error)
            *error = QStringLiteral("Give the server a name.");
        return false;
    }
    if (!isServerUrl(server.url)) {
        if (error)
            *error = QStringLiteral("\"%1\" is not a server address (sftp://, ftp://, smb://, webdav://...).").arg(server.url);
        return false;
    }
    QList<Server> list = items();
    bool replaced = false;
    for (Server &s : list) {
        if (s.name.compare(name, Qt::CaseInsensitive) == 0) {
            s = Server{name, server.url};
            replaced = true;
        }
    }
    if (!replaced)
        list << Server{name, server.url};
    setItems(list);
    return true;
}

bool Servers::remove(const QString &name)
{
    QList<Server> list = items();
    const qsizetype before = list.size();
    list.removeIf([&](const Server &s) { return s.name.compare(name, Qt::CaseInsensitive) == 0; });
    if (list.size() == before)
        return false;
    setItems(list);
    return true;
}

QString Servers::urlFor(const QString &name) const
{
    for (const Server &s : items()) {
        if (s.name.compare(name, Qt::CaseInsensitive) == 0)
            return s.url;
    }
    return {};
}

QString Servers::makeUrl(const QString &scheme, const QString &host, int port, const QString &user, const QString &folder)
{
    QUrl u;
    u.setScheme(scheme.toLower());
    u.setHost(host.trimmed());
    if (port > 0)
        u.setPort(port);
    if (!user.trimmed().isEmpty())
        u.setUserName(user.trimmed());
    QString path = folder.trimmed();
    if (!path.isEmpty() && !path.startsWith(QLatin1Char('/')))
        path.prepend(QLatin1Char('/'));
    u.setPath(path.isEmpty() ? QStringLiteral("/") : path);
    return u.toString();
}

QString Servers::suggestedName(const QString &url)
{
    const QUrl u(url);
    QString what = u.scheme().toUpper();
    if (what == QLatin1String("SMB"))
        what = QStringLiteral("Windows share");
    else if (what.startsWith(QLatin1String("WEBDAV")))
        what = QStringLiteral("WebDAV");
    QString place = u.host();
    const QString folder = u.path().section(QLatin1Char('/'), -1, -1, QString::SectionSkipEmpty);
    if (!folder.isEmpty())
        place += QLatin1Char(' ') + folder;
    return QStringLiteral("%1 (%2)").arg(place.isEmpty() ? QStringLiteral("Network") : place, what);
}
