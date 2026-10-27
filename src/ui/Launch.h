#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>

class AliasManager;
class QLocalServer;

// Starting DirDuo from outside: a folder or file on the command line (or a
// file:// link from the desktop, "Show in folder" from a browser or Steam),
// and handing that to the copy already running instead of starting another.
namespace Launch {

struct Target
{
    QString folder; // what to open
    QString select; // a file in it to select ("Show in folder"), or empty
};

// "~/Downloads", "file:///home/deck/x.png", "/sdcard" (an alias),
// "sftp://server/dir", "relative/path" (against cwd).
Target resolve(const QString &arg, const QString &cwd, const AliasManager *aliases);

// What one launch asks for.
struct Request
{
    QStringList args;     // as given; resolved by whoever runs them
    QString cwd;          // where they were given
    QStringList commands; // --cmd: run in the active lister
    bool tab = false;     // --tab: open in a new tab, not a new lister
    QString activationToken; // Wayland: lets the running copy come to the front

    QJsonObject toJson() const;
    static Request fromJson(const QJsonObject &o);
};

// Where the running copy listens. Inside the Flatpak this is the app's own
// runtime folder, which every instance of the app shares.
QString socketPath();

// Hands the request to a running copy; false when there is none (or it did
// not answer within timeoutMs), in which case this copy carries on itself.
bool sendToRunning(const Request &request, int timeoutMs = 1500);

} // namespace Launch

// Listens for later launches and passes each request on.
class InstanceServer : public QObject
{
    Q_OBJECT
public:
    explicit InstanceServer(QObject *parent = nullptr);
    ~InstanceServer() override;
    // Takes over the socket (a stale one from a crash is removed).
    bool listen(const QString &path = Launch::socketPath());
    QString path() const;

Q_SIGNALS:
    void requested(const Launch::Request &request);

private:
    QLocalServer *m_server;
};
