#pragma once

#include "core/FileItem.h"
#include "core/Job.h"

#include <QObject>

#include <memory>

// Lists a remote folder (or an archive) and reports the result the same way
// the local lister does.
class RemoteLister : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    virtual void open(const QString &location, bool isRefresh) = 0;
    virtual bool isBusy() const = 0;

Q_SIGNALS:
    void listed(const QString &location, const FileItemList &items, bool isRefresh);
    void failed(const QString &location, const QString &error);
};

// Everything that needs KIO. The core stays free of KDE libraries; the app
// installs a backend at start-up when it was built with KIO. Without one,
// remote locations report that they are not available.
class RemoteBackend
{
public:
    virtual ~RemoteBackend() = default;

    static RemoteBackend *instance();
    static void install(RemoteBackend *backend); // not owned

    virtual RemoteLister *createLister(QObject *parent) = 0;
    virtual std::shared_ptr<JobBase> createJob(int id, const JobSpec &spec, const QString &queueKey) = 0;

    // Quick operations, run to completion before returning.
    virtual bool rename(const QString &from, const QString &to, QString *error) = 0;
    virtual bool mkdir(const QString &location, QString *error) = 0;

    // "zip:/home/deck/roms.zip" for an archive the backend can open as a
    // folder; empty when it cannot.
    virtual QString archiveLocation(const QString &localFile) = 0;

    // Opens a remote file in its application (downloading it first if needed).
    virtual void openFile(const QString &location) = 0;
};
