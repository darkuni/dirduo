#pragma once

#include "core/RemoteBackend.h"

#include <QPointer>

class KCoreDirLister;
class QWidget;

// RemoteLister on KIO's KCoreDirLister: sftp, ftp, smb, fish, webdav, archives
// and every other protocol KIO has a worker for.
class KioLister : public RemoteLister
{
    Q_OBJECT
public:
    explicit KioLister(QObject *parent = nullptr);
    void open(const QString &location, bool isRefresh) override;
    bool isBusy() const override;

private:
    void emitListing();

    KCoreDirLister *m_lister;
    QString m_location;
    bool m_refresh = false;
};

// Installs itself as the RemoteBackend for the life of the application.
class KioBackend : public RemoteBackend
{
public:
    KioBackend();
    ~KioBackend() override;

    // Parent for password, host-key and certificate prompts.
    void setWindow(QWidget *window) { m_window = window; }
    QWidget *window() const { return m_window; }

    RemoteLister *createLister(QObject *parent) override;
    std::shared_ptr<JobBase> createJob(int id, const JobSpec &spec, const QString &queueKey) override;
    bool rename(const QString &from, const QString &to, QString *error) override;
    bool mkdir(const QString &location, QString *error) override;
    QString archiveLocation(const QString &localFile) override;
    void openFile(const QString &location) override;

    static KioBackend *kioInstance();

private:
    QPointer<QWidget> m_window;
};
