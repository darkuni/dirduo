#include "core/FolderSizes.h"

#include "core/Location.h"

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>

#include <sys/stat.h>

namespace {
FolderSizes *g_instance = nullptr;
}

FolderSizes::FolderSizes(QObject *parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    // Disk-bound work: a few at a time keeps the drive from thrashing.
    m_pool.setMaxThreadCount(2);
}

FolderSizes::~FolderSizes()
{
    *m_cancel = true;
    m_pool.waitForDone();
    if (g_instance == this)
        g_instance = nullptr;
}

FolderSizes *FolderSizes::instance()
{
    if (!g_instance)
        g_instance = new FolderSizes(qApp);
    return g_instance;
}

FolderSizes::Result FolderSizes::measure(const QString &path, const std::atomic<bool> *cancel)
{
    Result r;
    // Count each file once even when hard-linked, and never follow symlinks
    // (a link to / would otherwise measure the whole disk).
    QSet<QPair<quint64, quint64>> seen;
    QDirIterator it(path, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        if (cancel && *cancel)
            break;
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isSymLink()) {
            ++r.files;
            continue;
        }
        if (fi.isDir()) {
            ++r.folders;
            if (!fi.isReadable())
                r.complete = false;
            continue;
        }
        struct stat st;
        if (::lstat(QFile::encodeName(fi.filePath()).constData(), &st) == 0) {
            if (st.st_nlink > 1) {
                const auto key = qMakePair(static_cast<quint64>(st.st_dev), static_cast<quint64>(st.st_ino));
                if (seen.contains(key)) {
                    ++r.files;
                    continue;
                }
                seen.insert(key);
            }
            r.bytes += static_cast<qint64>(st.st_size);
        }
        ++r.files;
    }
    return r;
}

void FolderSizes::request(const QString &rawPath)
{
    const QString path = Location::clean(rawPath);
    if (Location::isRemote(path))
        return;
    {
        QMutexLocker lock(&m_mutex);
        if (m_cache.contains(path) || m_pending.contains(path))
            return;
        m_pending.insert(path);
    }
    QPointer<FolderSizes> self(this);
    auto cancel = m_cancel;
    m_pool.start([self, path, cancel]() {
        const Result r = measure(path, cancel.get());
        if (*cancel)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [self, path, r]() {
                if (!self)
                    return;
                {
                    QMutexLocker lock(&self->m_mutex);
                    if (!self->m_pending.remove(path))
                        return; // invalidated meanwhile
                    self->m_cache.insert(path, r);
                }
                Q_EMIT self->ready(path);
            },
            Qt::QueuedConnection);
    });
}

bool FolderSizes::has(const QString &path) const
{
    QMutexLocker lock(&m_mutex);
    return m_cache.contains(Location::clean(path));
}

FolderSizes::Result FolderSizes::result(const QString &path) const
{
    QMutexLocker lock(&m_mutex);
    return m_cache.value(Location::clean(path));
}

bool FolderSizes::isPending(const QString &path) const
{
    QMutexLocker lock(&m_mutex);
    return m_pending.contains(Location::clean(path));
}

void FolderSizes::invalidate(const QString &rawPath)
{
    const QString path = Location::clean(rawPath);
    QMutexLocker lock(&m_mutex);
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        // A change inside a folder changes the size of every folder above it.
        if (Location::isSameOrInside(it.key(), path) || Location::isSameOrInside(path, it.key()))
            it = m_cache.erase(it);
        else
            ++it;
    }
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (Location::isSameOrInside(*it, path) || Location::isSameOrInside(path, *it))
            it = m_pending.erase(it);
        else
            ++it;
    }
}

void FolderSizes::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_pending.clear();
}
