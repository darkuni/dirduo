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
    bool truncated = false;
};
} // namespace

FileItemList DirLister::listRecursive(const QString &path, bool includeFolders, int maxItems, bool *truncated,
                                      QString *error, const std::atomic<quint64> *generation, quint64 expected)
{
    FileItemList items;
    if (truncated)
        *truncated = false;
    const QFileInfo dirInfo(path);
    if (!dirInfo.isDir() || !dirInfo.isReadable()) {
        if (error)
            *error = dirInfo.exists() ? QStringLiteral("Permission denied.") : QStringLiteral("The folder does not exist.");
        return items;
    }
    const QString root = QDir::cleanPath(path);
    QDirIterator it(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    int n = 0;
    while (it.hasNext()) {
        if (generation && generation->load() != expected)
            return {}; // superseded
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isDir() && !includeFolders)
            continue;
        FileItem item = FileItem::fromFileInfo(fi);
        const QString parent = fi.absolutePath();
        item.relPath = parent == root ? QString() : parent.mid(root.size() + 1);
        // A hidden folder anywhere up the path hides the item too.
        if (!item.isHidden && item.relPath.contains(QLatin1String("/.")))
            item.isHidden = true;
        if (!item.isHidden && item.relPath.startsWith(QLatin1Char('.')))
            item.isHidden = true;
        items.append(item);
        if (++n >= maxItems) {
            if (truncated)
                *truncated = true;
            break;
        }
    }
    return items;
}

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

void DirLister::setFlatMode(FlatMode mode)
{
    if (m_flat == mode)
        return;
    m_flat = mode;
    if (!m_path.isEmpty())
        open(m_path);
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
        m_truncated = r.truncated;
        if (!r.error.isEmpty())
            Q_EMIT failed(path, r.error);
        else
            Q_EMIT listed(path, r.items, isRefresh);
        if (m_refreshPending)
            refresh();
    });
    const FlatMode flat = m_flat;
    if (!m_liveGeneration)
        m_liveGeneration = std::make_shared<std::atomic<quint64>>(0);
    m_liveGeneration->store(gen);
    auto live = m_liveGeneration;
    watcher->setFuture(QtConcurrent::run([path, flat, live, gen]() {
        ListResult r;
        if (flat == FlatMode::Off)
            r.items = DirLister::listNow(path, &r.error);
        else
            r.items = DirLister::listRecursive(path, flat != FlatMode::MixedNoFolders, kFlatLimit, &r.truncated,
                                               &r.error, live.get(), gen);
        return r;
    }));
}
