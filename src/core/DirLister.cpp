#include "core/DirLister.h"

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

void DirLister::open(const QString &path)
{
    m_path = QDir::cleanPath(path);
    m_debounce.stop();
    start(false);
    watch();
}

void DirLister::refresh()
{
    if (m_path.isEmpty())
        return;
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
    if (QFileInfo(m_path).isDir())
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
