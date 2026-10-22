#include "core/DirLister.h"

#include "core/Location.h"
#include "core/RemoteBackend.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace {
struct ListResult
{
    FileItemList items;
    QString error;
};
} // namespace

DirLister::DirLister(QObject *parent)
    : QObject(parent)
{
    static const int reg1 = qRegisterMetaType<FileItem>();
    static const int reg2 = qRegisterMetaType<FileItemList>();
    Q_UNUSED(reg1)
    Q_UNUSED(reg2)

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(250);
    connect(&m_debounce, &QTimer::timeout, this, &DirLister::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this](const QString &) {
        m_debounce.start();
    });
}

FileItemList DirLister::listNow(const QString &path, QString *error)
{
    FileItemList items;
    const QFileInfo dirInfo(path);
    if (!dirInfo.exists()) {
        if (error)
            *error = QStringLiteral("The folder does not exist.");
        return items;
    }
    if (!dirInfo.isDir()) {
        if (error)
            *error = QStringLiteral("This is not a folder.");
        return items;
    }
    if (!dirInfo.isReadable() || !dirInfo.isExecutable()) {
        if (error)
            *error = QStringLiteral("Permission denied.");
        return items;
    }

    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    while (it.hasNext()) {
        it.next();
        items.append(FileItem::fromFileInfo(it.fileInfo()));
    }
    return items;
}

bool DirLister::isBusy() const
{
    if (Location::isRemote(m_path))
        return m_remote && m_remote->isBusy();
    return m_busy;
}

void DirLister::open(const QString &path)
{
    m_path = Location::clean(path);
    m_debounce.stop();
    ++m_generation; // drop any local listing still in flight
    if (Location::isRemote(m_path)) {
        watch(); // stops watching the previous local folder
        openRemote(false);
        return;
    }
    start(false);
    watch();
}

void DirLister::openRemote(bool isRefresh)
{
    RemoteBackend *backend = RemoteBackend::instance();
    if (!backend) {
        Q_EMIT failed(m_path, QStringLiteral("Remote folders need the KIO build of Linopus."));
        return;
    }
    if (!m_remote) {
        m_remote = backend->createLister(this);
        connect(m_remote, &RemoteLister::listed, this, [this](const QString &loc, const FileItemList &items, bool refresh) {
            if (loc == m_path)
                Q_EMIT listed(loc, items, refresh);
        });
        connect(m_remote, &RemoteLister::failed, this, [this](const QString &loc, const QString &error) {
            if (loc == m_path)
                Q_EMIT failed(loc, error);
        });
    }
    m_remote->open(m_path, isRefresh);
}

void DirLister::refresh()
{
    if (m_path.isEmpty())
        return;
    if (Location::isRemote(m_path)) {
        openRemote(true);
        return;
    }
    if (m_busy) {
        m_refreshPending = true;
        return;
    }
    start(true);
}

void DirLister::watch()
{
    const QStringList watched = m_watcher.directories();
    if (!watched.isEmpty())
        m_watcher.removePaths(watched);
    if (Location::isLocal(m_path) && QFileInfo(m_path).isDir())
        m_watcher.addPath(m_path);
}

void DirLister::start(bool isRefresh)
{
    const quint64 gen = ++m_generation;
    const QString path = m_path;
    m_busy = true;
    m_refreshPending = false;

    auto *watcher = new QFutureWatcher<ListResult>(this);
    connect(watcher, &QFutureWatcher<ListResult>::finished, this, [this, watcher, gen, path, isRefresh]() {
        watcher->deleteLater();
        if (gen != m_generation)
            return; // superseded by a newer request
        m_busy = false;
        const ListResult r = watcher->result();
        if (!r.error.isEmpty())
            Q_EMIT failed(path, r.error);
        else
            Q_EMIT listed(path, r.items, isRefresh);
        if (m_refreshPending)
            refresh();
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        ListResult r;
        r.items = DirLister::listNow(path, &r.error);
        return r;
    }));
}
