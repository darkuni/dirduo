#include "ui/Launch.h"

#include "core/AliasManager.h"
#include "core/Location.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPointer>
#include <QStandardPaths>
#include <QUrl>

#include <unistd.h>

namespace Launch {

Target resolve(const QString &rawArg, const QString &cwd, const AliasManager *aliases)
{
    Target t;
    QString arg = rawArg.trimmed();
    if (arg.isEmpty())
        return t;
    // Links from the desktop: file:// is a local path, other schemes are
    // servers and stay as they are.
    if (arg.contains(QLatin1String("://"))) {
        const QUrl url(arg);
        if (url.isLocalFile()) {
            arg = url.toLocalFile();
        } else if (url.isValid() && !url.scheme().isEmpty()) {
            t.folder = Location::clean(arg);
            return t;
        }
    }
    QString path = aliases ? aliases->resolve(arg) : arg;
    if (path.isEmpty())
        path = arg;
    if (path == QLatin1String("~") || path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    if (Location::isRemote(path)) {
        t.folder = Location::clean(path);
        return t;
    }
    if (!QDir::isAbsolutePath(path))
        path = QDir(cwd.isEmpty() ? QDir::currentPath() : cwd).absoluteFilePath(path);
    path = QDir::cleanPath(path);
    const QFileInfo fi(path);
    if (fi.isDir()) {
        t.folder = path;
    } else if (fi.exists() || fi.isSymLink()) {
        // A file: its folder, with the file selected.
        t.folder = fi.absolutePath();
        t.select = path;
    } else {
        // Not there (yet): the nearest folder that is.
        QString p = path;
        while (!p.isEmpty() && p != QLatin1String("/") && !QFileInfo(p).isDir())
            p = QFileInfo(p).absolutePath();
        t.folder = p.isEmpty() ? QDir::homePath() : p;
    }
    return t;
}

QJsonObject Request::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("args"), QJsonArray::fromStringList(args));
    o.insert(QStringLiteral("cwd"), cwd);
    o.insert(QStringLiteral("commands"), QJsonArray::fromStringList(commands));
    o.insert(QStringLiteral("tab"), tab);
    o.insert(QStringLiteral("token"), activationToken);
    return o;
}

Request Request::fromJson(const QJsonObject &o)
{
    Request r;
    for (const QJsonValue &v : o.value(QStringLiteral("args")).toArray())
        r.args << v.toString();
    r.cwd = o.value(QStringLiteral("cwd")).toString();
    for (const QJsonValue &v : o.value(QStringLiteral("commands")).toArray())
        r.commands << v.toString();
    r.tab = o.value(QStringLiteral("tab")).toBool();
    r.activationToken = o.value(QStringLiteral("token")).toString();
    return r;
}

QString socketPath()
{
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    const QString appId = qEnvironmentVariable("FLATPAK_ID");
    if (!runtime.isEmpty() && !appId.isEmpty())
        return runtime + QStringLiteral("/app/") + appId + QStringLiteral("/linopus.sock");
    if (!runtime.isEmpty())
        return runtime + QStringLiteral("/linopus.sock");
    return QDir::tempPath() + QStringLiteral("/linopus-%1.sock").arg(::getuid());
}

bool sendToRunning(const Request &request, int timeoutMs)
{
    QLocalSocket socket;
    socket.connectToServer(socketPath());
    if (!socket.waitForConnected(timeoutMs))
        return false;
    socket.write(QJsonDocument(request.toJson()).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(timeoutMs))
        return false;
    // Wait for "ok", so a copy that is shutting down is not trusted with it.
    // The answer may come in pieces, or together with the hang-up.
    QByteArray reply;
    QElapsedTimer clock;
    clock.start();
    while (!reply.contains('\n') && clock.elapsed() < timeoutMs) {
        if (socket.bytesAvailable() == 0) {
            const bool more = socket.waitForReadyRead(int(qMax<qint64>(1, timeoutMs - clock.elapsed())));
            reply += socket.readAll();
            if (!more && socket.state() != QLocalSocket::ConnectedState)
                break; // gone; whatever it said is in reply
            continue;
        }
        reply += socket.readAll();
    }
    return reply.startsWith("ok");
}

} // namespace Launch

InstanceServer::InstanceServer(QObject *parent)
    : QObject(parent)
    , m_server(new QLocalServer(this))
{
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    connect(m_server, &QLocalServer::newConnection, this, [this]() {
        while (QLocalSocket *s = m_server->nextPendingConnection()) {
            QPointer<QLocalSocket> socket(s);
            connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            connect(s, &QLocalSocket::readyRead, this, [this, socket]() {
                if (!socket || !socket->canReadLine())
                    return;
                const QByteArray line = socket->readLine();
                const QJsonDocument doc = QJsonDocument::fromJson(line);
                if (!doc.isObject()) {
                    socket->write("bad\n");
                    socket->disconnectFromServer();
                    return;
                }
                // The new launch hangs up once it has read this.
                socket->write("ok\n");
                socket->flush();
                Q_EMIT requested(Launch::Request::fromJson(doc.object()));
            });
        }
    });
}

InstanceServer::~InstanceServer()
{
    m_server->close();
}

bool InstanceServer::listen(const QString &path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    // Someone answering there? Then it is not ours to take. (Qt would
    // quietly replace a live socket: it makes it elsewhere and renames it.)
    if (QFileInfo::exists(path)) {
        QLocalSocket probe;
        probe.connectToServer(path);
        if (probe.waitForConnected(300)) {
            probe.disconnectFromServer();
            return false;
        }
        QLocalServer::removeServer(path); // left over from a crash
    }
    return m_server->listen(path);
}

QString InstanceServer::path() const
{
    return m_server->fullServerName();
}
