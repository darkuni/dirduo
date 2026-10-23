#pragma once

#include <QList>
#include <QObject>
#include <QString>

// The server address book: SFTP, FTP, SMB and WebDAV places with a name, kept
// in servers.json. Passwords are not kept here: KIO asks for them and the
// desktop wallet remembers them.
struct Server
{
    QString name;
    QString url; // sftp://deck@nas:22/media
    bool operator==(const Server &o) const { return name == o.name && url == o.url; }
};

class Servers : public QObject
{
    Q_OBJECT
public:
    static Servers *instance();

    QList<Server> items() const;
    void setItems(const QList<Server> &items);
    // Adds or, when the name is taken, replaces. False with error for a bad
    // name or an address that is not a server.
    bool add(const Server &server, QString *error = nullptr);
    bool remove(const QString &name);
    QString urlFor(const QString &name) const; // empty when unknown

    // Builds an address from its parts: scheme sftp, ftp, smb, webdav or
    // webdavs; port 0 for the default; folder with or without a leading /.
    static QString makeUrl(const QString &scheme, const QString &host, int port, const QString &user, const QString &folder);
    // A name for an address: "nas (SFTP)".
    static QString suggestedName(const QString &url);
    static bool isServerUrl(const QString &url);

    QString file() const;

Q_SIGNALS:
    void changed();

private:
    Servers() = default;
};
