#include "core/FileIndex.h"

#include "core/Location.h"
#include "core/Paths.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QSet>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSettings>
#include <QThread>
#include <QtConcurrent>

#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr quint32 kMagic = 0x44444958; // "DDIX"
constexpr qint32 kVersion = 1;
constexpr int kMaxEntries = 8 * 1000 * 1000;
constexpr int kSortCap = 20000;

struct Entry
{
    quint32 folder;  // index into Data::folders: the folder it is in
    quint32 nameOff; // into Data::names
    quint16 nameLen;
    quint8 isDir;
    quint8 pad = 0;
    qint64 size;
    qint64 mtime;    // seconds since 1970
};

inline uchar lower(uchar c)
{
    return (c >= 'A' && c <= 'Z') ? uchar(c + 32) : c;
}

// Case-insensitive (for ASCII) "has needle in it"; needle is lowercase.
bool containsCi(const char *hay, int hayLen, const QByteArray &needle)
{
    const int n = int(needle.size());
    if (n == 0)
        return true;
    if (n > hayLen)
        return false;
    const uchar first = uchar(needle.at(0));
    for (int i = 0; i <= hayLen - n; ++i) {
        if (lower(uchar(hay[i])) != first)
            continue;
        int j = 1;
        while (j < n && lower(uchar(hay[i + j])) == uchar(needle.at(j)))
            ++j;
        if (j == n)
            return true;
    }
    return false;
}

bool startsWithCi(const char *s, int len, const QByteArray &prefix)
{
    if (prefix.size() > len)
        return false;
    for (int i = 0; i < prefix.size(); ++i)
        if (lower(uchar(s[i])) != uchar(prefix.at(i)))
            return false;
    return true;
}

bool endsWithCi(const QByteArray &s, const QByteArray &suffix)
{
    if (suffix.size() > s.size())
        return false;
    const int off = int(s.size() - suffix.size());
    for (int i = 0; i < suffix.size(); ++i)
        if (lower(uchar(s.at(off + i))) != uchar(suffix.at(i)))
            return false;
    return true;
}

// Wildcards * and ?, case-insensitive for ASCII; pattern is lowercase.
bool globCi(const char *s, int n, const char *p, int m)
{
    int si = 0, pi = 0, star = -1, mark = 0;
    while (si < n) {
        if (pi < m && (p[pi] == '?' || uchar(p[pi]) == lower(uchar(s[si])))) {
            ++si;
            ++pi;
        } else if (pi < m && p[pi] == '*') {
            star = pi++;
            mark = si;
        } else if (star >= 0) {
            pi = star + 1;
            si = ++mark;
        } else {
            return false;
        }
    }
    while (pi < m && p[pi] == '*')
        ++pi;
    return pi == m;
}

struct Query
{
    QList<QByteArray> words;   // in the name
    QList<QByteArray> globs;   // wildcards on the name
    QList<QByteArray> paths;   // with a / : folder path and name
    QList<QByteArray> exts;    // ext:
    bool filesOnly = false;
    bool foldersOnly = false;
    bool isEmpty() const { return words.isEmpty() && globs.isEmpty() && paths.isEmpty() && exts.isEmpty() && !filesOnly && !foldersOnly; }
};

Query parse(const QString &text)
{
    Query q;
    // Words split on spaces; "quoted words" stay together.
    QStringList parts;
    QString cur;
    bool quoted = false;
    for (const QChar c : text) {
        if (c == QLatin1Char('"')) {
            quoted = !quoted;
        } else if (c.isSpace() && !quoted) {
            if (!cur.isEmpty())
                parts << cur;
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.isEmpty())
        parts << cur;
    for (const QString &raw : std::as_const(parts)) {
        const QString w = raw.toLower();
        if (w == QLatin1String("file:") || w == QLatin1String("files:")) {
            q.filesOnly = true;
        } else if (w == QLatin1String("folder:") || w == QLatin1String("folders:") || w == QLatin1String("dir:")) {
            q.foldersOnly = true;
        } else if (w.startsWith(QLatin1String("ext:"))) {
            for (const QString &e : w.mid(4).split(QLatin1Char(';'), Qt::SkipEmptyParts))
                q.exts << QByteArray(".") + (e.startsWith(QLatin1Char('.')) ? e.mid(1) : e).toUtf8();
        } else if (w.contains(QLatin1Char('/'))) {
            q.paths << w.toUtf8();
        } else if (w.contains(QLatin1Char('*')) || w.contains(QLatin1Char('?'))) {
            q.globs << w.toUtf8();
        } else {
            q.words << w.toUtf8();
        }
    }
    return q;
}

} // namespace

struct FileIndex::Data
{
    QVector<QByteArray> folders; // full paths
    QHash<QByteArray, int> folderSlot;
    QVector<Entry> entries;
    QByteArray names;
    QStringList roots;
    bool hidden = false;
    QDateTime builtAt;

    void addEntry(quint32 folder, const char *name, int len, bool dir, qint64 size, qint64 mtime)
    {
        Entry e;
        e.folder = folder;
        e.nameOff = quint32(names.size());
        e.nameLen = quint16(qMin(len, 65535));
        e.isDir = dir ? 1 : 0;
        e.size = size;
        e.mtime = mtime;
        names.append(name, e.nameLen);
        entries.append(e);
    }
};

struct FileIndex::Overlay
{
    QVector<bool> patched; // by Data folder slot: its contents come from items
    QVector<bool> dead;    // by Data folder slot: gone
    QHash<QByteArray, QList<OverlayItem>> items; // folder path -> what is in it
};

namespace {

struct BuildSettings
{
    QStringList roots;
    bool hidden = false;
    QSet<QByteArray> exclude;
};

std::atomic<bool> g_stop{false};

void walk(const QByteArray &dirPath, FileIndex::Data &d, const BuildSettings &s)
{
    if (g_stop || d.entries.size() >= kMaxEntries)
        return;
    DIR *dir = opendir(dirPath.constData());
    if (!dir)
        return;
    const int dfd = dirfd(dir);
    const quint32 slot = quint32(d.folders.size());
    d.folders.append(dirPath);
    d.folderSlot.insert(dirPath, int(slot));
    QList<QByteArray> subdirs;
    while (dirent *de = readdir(dir)) {
        const char *name = de->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;
        if (!s.hidden && name[0] == '.')
            continue;
        struct stat st;
        if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
            continue;
        const bool isDir = S_ISDIR(st.st_mode);
        const int len = int(strlen(name));
        if (isDir && s.exclude.contains(QByteArray::fromRawData(name, len)))
            continue;
        d.addEntry(slot, name, len, isDir, isDir ? 0 : qint64(st.st_size), qint64(st.st_mtime));
        if (isDir)
            subdirs << (dirPath.endsWith('/') ? dirPath + name : dirPath + '/' + name);
    }
    closedir(dir);
    for (const QByteArray &sub : std::as_const(subdirs))
        walk(sub, d, s);
}

QList<OverlayItem> listOne(const QByteArray &dirPath, const BuildSettings &s)
{
    QList<OverlayItem> out;
    DIR *dir = opendir(dirPath.constData());
    if (!dir)
        return out;
    const int dfd = dirfd(dir);
    while (dirent *de = readdir(dir)) {
        const char *name = de->d_name;
        if (name[0] == '.' && (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;
        if (!s.hidden && name[0] == '.')
            continue;
        struct stat st;
        if (fstatat(dfd, name, &st, AT_SYMLINK_NOFOLLOW) != 0)
            continue;
        OverlayItem it;
        it.name = QByteArray(name);
        it.isDir = S_ISDIR(st.st_mode);
        if (it.isDir && s.exclude.contains(it.name))
            continue;
        it.size = it.isDir ? 0 : qint64(st.st_size);
        it.mtime = qint64(st.st_mtime);
        out << it;
    }
    closedir(dir);
    return out;
}

std::shared_ptr<const FileIndex::Data> build(const BuildSettings &s, const QString &cacheFile)
{
    QThread::currentThread()->setPriority(QThread::LowestPriority);
    auto d = std::make_shared<FileIndex::Data>();
    d->roots = s.roots;
    d->hidden = s.hidden;
    for (const QString &r : s.roots) {
        QByteArray root = QFile::encodeName(QDir::cleanPath(r));
        if (QFileInfo(r).isDir())
            walk(root, *d, s);
    }
    d->builtAt = QDateTime::currentDateTime();
    if (g_stop)
        return d;
    // Kept for next time.
    QDir().mkpath(QFileInfo(cacheFile).absolutePath());
    QSaveFile f(cacheFile);
    if (f.open(QIODevice::WriteOnly)) {
        QDataStream out(&f);
        out << kMagic << kVersion << d->roots << d->hidden << d->builtAt << d->folders << qint64(d->entries.size());
        out.writeRawData(reinterpret_cast<const char *>(d->entries.constData()), int(d->entries.size() * sizeof(Entry)));
        out << d->names;
        f.commit();
    }
    return d;
}

std::shared_ptr<const FileIndex::Data> loadCache(const QString &cacheFile)
{
    QThread::currentThread()->setPriority(QThread::LowestPriority);
    QFile f(cacheFile);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QDataStream in(&f);
    quint32 magic = 0;
    qint32 version = 0;
    in >> magic >> version;
    if (magic != kMagic || version != kVersion)
        return {};
    auto d = std::make_shared<FileIndex::Data>();
    qint64 n = 0;
    in >> d->roots >> d->hidden >> d->builtAt >> d->folders >> n;
    if (in.status() != QDataStream::Ok || n < 0 || n > kMaxEntries)
        return {};
    d->entries.resize(n);
    if (in.readRawData(reinterpret_cast<char *>(d->entries.data()), int(n * sizeof(Entry))) != int(n * sizeof(Entry)))
        return {};
    in >> d->names;
    if (in.status() != QDataStream::Ok)
        return {};
    d->folderSlot.reserve(d->folders.size());
    for (int i = 0; i < d->folders.size(); ++i)
        d->folderSlot.insert(d->folders.at(i), i);
    return d;
}

} // namespace

QString FileIndex::Hit::path() const
{
    return Location::join(folder, name);
}

FileIndex *FileIndex::instance()
{
    static FileIndex *i = new FileIndex;
    return i;
}

FileIndex::FileIndex()
{
    loadSettings();
    m_timer.setInterval(60 * 1000);
    connect(&m_timer, &QTimer::timeout, this, &FileIndex::scheduleCheck);
    connect(&m_watcher, &QFutureWatcher<std::shared_ptr<const Data>>::finished, this, [this]() {
        finishBuild(m_watcher.result(), m_watcherFromCache);
    });
    connect(qApp, &QCoreApplication::aboutToQuit, this, []() { g_stop = true; });
}

QStringList FileIndex::defaultRoots()
{
    QStringList out{QDir::homePath()};
    // The SD card and USB drives (SteamOS and KDE mount them here).
    const QString user = qEnvironmentVariable("USER");
    if (!user.isEmpty() && QFileInfo(QStringLiteral("/run/media/") + user).isDir())
        out << QStringLiteral("/run/media/") + user;
    else if (QFileInfo(QStringLiteral("/run/media")).isDir())
        out << QStringLiteral("/run/media");
    return out;
}

QStringList FileIndex::defaultExclude()
{
    return {QStringLiteral("node_modules"), QStringLiteral("__pycache__"), QStringLiteral("shadercache"), QStringLiteral("lost+found")};
}

void FileIndex::loadSettings()
{
    QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Index"));
    m_settings.enabled = s.value(QStringLiteral("enabled"), true).toBool();
    m_settings.roots = s.value(QStringLiteral("roots")).toStringList();
    m_settings.hidden = s.value(QStringLiteral("hidden"), false).toBool();
    m_settings.rebuildMinutes = qBound(0, s.value(QStringLiteral("rebuildMinutes"), 60).toInt(), 24 * 60);
    m_settings.exclude = s.value(QStringLiteral("exclude"), defaultExclude()).toStringList();
}

QStringList FileIndex::roots() const
{
    return m_settings.roots.isEmpty() ? defaultRoots() : m_settings.roots;
}

QString FileIndex::cacheFile() const
{
    return Paths::userCacheHome() + QStringLiteral("/dirduo/index.bin");
}

void FileIndex::setSettings(const Settings &st)
{
    const Settings before = m_settings;
    m_settings = st;
    QSettings s(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("Index"));
    s.setValue(QStringLiteral("enabled"), st.enabled);
    s.setValue(QStringLiteral("roots"), st.roots);
    s.setValue(QStringLiteral("hidden"), st.hidden);
    s.setValue(QStringLiteral("rebuildMinutes"), st.rebuildMinutes);
    s.setValue(QStringLiteral("exclude"), st.exclude);
    if (!st.enabled) {
        m_timer.stop();
        QMutexLocker lock(&m_lock);
        m_data.reset();
        m_overlay.reset();
        lock.unlock();
        QFile::remove(cacheFile());
        Q_EMIT changed();
        return;
    }
    if (!m_started) {
        start();
        return;
    }
    if (before.roots != st.roots || before.hidden != st.hidden || before.exclude != st.exclude || !before.enabled)
        rebuild();
}

void FileIndex::start()
{
    m_started = true;
    if (!m_settings.enabled)
        return;
    m_timer.start();
    if (m_building)
        return;
    m_building = true;
    m_watcherFromCache = true;
    Q_EMIT buildingChanged(true);
    const QString file = cacheFile();
    m_watcher.setFuture(QtConcurrent::run([file]() { return loadCache(file); }));
}

void FileIndex::rebuild()
{
    if (!m_settings.enabled)
        return;
    if (m_watcher.isRunning()) {
        m_rebuildPending = true; // after the build or load under way
        return;
    }
    m_building = true;
    m_watcherFromCache = false;
    Q_EMIT buildingChanged(true);
    BuildSettings bs;
    bs.roots = roots();
    bs.hidden = m_settings.hidden;
    for (const QString &e : m_settings.exclude)
        bs.exclude.insert(e.toUtf8());
    const QString file = cacheFile();
    m_watcher.setFuture(QtConcurrent::run([bs, file]() { return build(bs, file); }));
}

void FileIndex::finishBuild(std::shared_ptr<const Data> d, bool fromCache)
{
    m_building = false;
    const bool stale = fromCache
        && (!d || d->roots != roots() || d->hidden != m_settings.hidden
            || (m_settings.rebuildMinutes > 0 && d->builtAt.secsTo(QDateTime::currentDateTime()) > m_settings.rebuildMinutes * 60));
    if (d && m_settings.enabled) {
        QMutexLocker lock(&m_lock);
        m_data = d;
        auto o = std::make_shared<Overlay>();
        o->patched.resize(d->folders.size());
        o->dead.resize(d->folders.size());
        m_overlay = o;
    }
    Q_EMIT buildingChanged(false);
    Q_EMIT changed();
    const bool again = m_rebuildPending;
    m_rebuildPending = false;
    if ((stale || again) && m_settings.enabled)
        rebuild();
}

void FileIndex::scheduleCheck()
{
    if (!m_settings.enabled || m_building || m_settings.rebuildMinutes <= 0)
        return;
    const QDateTime at = builtAt();
    if (!at.isValid() || at.secsTo(QDateTime::currentDateTime()) >= m_settings.rebuildMinutes * 60)
        rebuild();
}

bool FileIndex::isReady() const
{
    QMutexLocker lock(&m_lock);
    return bool(m_data);
}

qint64 FileIndex::count() const
{
    QMutexLocker lock(&m_lock);
    if (!m_data)
        return 0;
    qint64 n = m_data->entries.size();
    if (m_overlay)
        for (auto it = m_overlay->items.cbegin(); it != m_overlay->items.cend(); ++it)
            n += it.value().size();
    return n;
}

QDateTime FileIndex::builtAt() const
{
    QMutexLocker lock(&m_lock);
    return m_data ? m_data->builtAt : QDateTime();
}

bool FileIndex::waitIdle(int ms)
{
    QElapsedTimer t;
    t.start();
    while ((m_building || m_watcher.isRunning()) && t.elapsed() < ms) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    QCoreApplication::processEvents();
    return !m_building;
}

bool FileIndex::covers(const QString &path) const
{
    if (!Location::isLocal(path))
        return false;
    for (const QString &r : roots()) {
        const QString root = QDir::cleanPath(r);
        if (path == root || path.startsWith(root.endsWith(QLatin1Char('/')) ? root : root + QLatin1Char('/'))) {
            // Inside a hidden folder, when those are left out: not indexed.
            if (!m_settings.hidden && path.mid(root.size()).contains(QLatin1String("/.")))
                return false;
            return true;
        }
    }
    return false;
}

QStringList FileIndex::unknownSubfolders(const QString &folder, const QList<OverlayItem> &items) const
{
    QMutexLocker lock(&m_lock);
    QStringList out;
    if (!m_data)
        return out;
    for (const OverlayItem &it : items) {
        if (!it.isDir)
            continue;
        const QByteArray p = QFile::encodeName(Location::join(folder, QString::fromUtf8(it.name)));
        if (!m_data->folderSlot.contains(p) && !(m_overlay && m_overlay->items.contains(p)))
            out << QFile::decodeName(p);
    }
    return out;
}

void FileIndex::folderListed(const QString &path, const FileItemList &items)
{
    if (!m_settings.enabled || !isReady() || !covers(path))
        return;
    QList<OverlayItem> list;
    for (const FileItem &f : items) {
        if (!f.relPath.isEmpty() || f.depth != 0 || Location::parent(f.path) != path)
            continue; // flat views and opened subfolders: only what is right in it
        if (!m_settings.hidden && f.name.startsWith(QLatin1Char('.')))
            continue;
        if (f.isDir && m_settings.exclude.contains(f.name))
            continue;
        OverlayItem it;
        it.name = f.name.toUtf8();
        it.isDir = f.isDir && !f.isSymlink;
        it.size = f.isDir ? 0 : f.size;
        it.mtime = f.modified.toSecsSinceEpoch();
        list << it;
    }
    const QStringList unknown = unknownSubfolders(path, list);
    applyPatches({{QFile::encodeName(path), list}});
    for (const QString &sub : unknown)
        rescan(sub, true);
}

void FileIndex::rescan(const QString &folder, bool recursive)
{
    if (!m_settings.enabled || !isReady() || !covers(folder))
        return;
    BuildSettings bs;
    bs.hidden = m_settings.hidden;
    for (const QString &e : m_settings.exclude)
        bs.exclude.insert(e.toUtf8());
    std::shared_ptr<const Data> data;
    std::shared_ptr<const Overlay> overlay;
    {
        QMutexLocker lock(&m_lock);
        data = m_data;
        overlay = m_overlay;
    }
    const QByteArray root = QFile::encodeName(QDir::cleanPath(folder));
    auto *watcher = new QFutureWatcher<QHash<QByteArray, QList<OverlayItem>>>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher]() {
        applyPatches(watcher->result());
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([root, recursive, bs, data, overlay]() {
        QThread::currentThread()->setPriority(QThread::LowestPriority);
        QHash<QByteArray, QList<OverlayItem>> out;
        QList<QByteArray> todo{root};
        while (!todo.isEmpty() && out.size() < 20000) {
            const QByteArray dir = todo.takeFirst();
            const QList<OverlayItem> items = listOne(dir, bs);
            if (items.isEmpty() && !QFileInfo(QFile::decodeName(dir)).isDir())
                continue;
            out.insert(dir, items);
            if (!recursive)
                break;
            for (const OverlayItem &it : items) {
                if (!it.isDir)
                    continue;
                const QByteArray sub = dir + '/' + it.name;
                // Folders the index has are read again only for this one.
                if (!data->folderSlot.contains(sub) && !(overlay && overlay->items.contains(sub)))
                    todo << sub;
            }
        }
        return out;
    }));
}

void FileIndex::applyPatches(const QHash<QByteArray, QList<OverlayItem>> &patches)
{
    if (patches.isEmpty())
        return;
    QMutexLocker lock(&m_lock);
    if (!m_data || !m_overlay)
        return;
    auto o = std::make_shared<Overlay>(*m_overlay); // copy: searches running keep the old one
    for (auto p = patches.cbegin(); p != patches.cend(); ++p) {
        const QByteArray &folder = p.key();
        // Subfolders it had that are gone now: everything under them goes too.
        QSet<QByteArray> before;
        const int slot = m_data->folderSlot.value(folder, -1);
        if (slot >= 0 && !o->patched.value(slot)) {
            for (const Entry &e : m_data->entries)
                if (int(e.folder) == slot && e.isDir)
                    before.insert(QByteArray(m_data->names.constData() + e.nameOff, e.nameLen));
        } else if (o->items.contains(folder)) {
            for (const OverlayItem &it : o->items.value(folder))
                if (it.isDir)
                    before.insert(it.name);
        }
        for (const OverlayItem &it : p.value())
            if (it.isDir)
                before.remove(it.name);
        for (const QByteArray &goneName : std::as_const(before)) {
            const QByteArray gone = folder + '/' + goneName;
            const QByteArray under = gone + '/';
            for (int i = 0; i < m_data->folders.size(); ++i)
                if (m_data->folders.at(i) == gone || m_data->folders.at(i).startsWith(under))
                    o->dead[i] = true;
            for (auto it = o->items.begin(); it != o->items.end();) {
                if (it.key() == gone || it.key().startsWith(under))
                    it = o->items.erase(it);
                else
                    ++it;
            }
        }
        if (slot >= 0)
            o->patched[slot] = true;
        o->items.insert(folder, p.value());
    }
    m_overlay = o;
    lock.unlock();
    Q_EMIT changed();
}

FileIndex::Result FileIndex::search(const QString &text, int limit, const std::atomic<bool> *cancel) const
{
    Result r;
    std::shared_ptr<const Data> d;
    std::shared_ptr<const Overlay> o;
    {
        QMutexLocker lock(&m_lock);
        d = m_data;
        o = m_overlay;
    }
    const Query q = parse(text);
    if (!d || q.isEmpty())
        return r;

    struct Match
    {
        const QByteArray *folder;
        const char *name;
        int nameLen;
        bool isDir;
        qint64 size;
        qint64 mtime;
    };
    QList<Match> found;
    auto test = [&q](const QByteArray &folder, const char *name, int len, bool isDir) {
        if ((q.filesOnly && isDir) || (q.foldersOnly && !isDir))
            return false;
        for (const QByteArray &w : q.words)
            if (!containsCi(name, len, w))
                return false;
        for (const QByteArray &g : q.globs)
            if (!globCi(name, len, g.constData(), int(g.size())))
                return false;
        if (!q.exts.isEmpty()) {
            bool any = false;
            const QByteArray n = QByteArray::fromRawData(name, len);
            for (const QByteArray &e : q.exts)
                any = any || endsWithCi(n, e);
            if (!any)
                return false;
        }
        for (const QByteArray &pth : q.paths) {
            // In the folder path, or across it into the name.
            if (containsCi(folder.constData(), int(folder.size()), pth))
                continue;
            bool ok = false;
            for (int k = 0; k < pth.size() && !ok; ++k) {
                if (pth.at(k) != '/')
                    continue;
                ok = endsWithCi(folder, pth.left(k)) && startsWithCi(name, len, pth.mid(k + 1));
            }
            if (!ok)
                return false;
        }
        return true;
    };
    const bool overlayOn = o && !o->patched.isEmpty();
    for (int i = 0; i < d->entries.size(); ++i) {
        if ((i & 0xFFFF) == 0 && cancel && *cancel)
            return {};
        const Entry &e = d->entries.at(i);
        if (overlayOn && (o->patched.at(e.folder) || o->dead.at(e.folder)))
            continue;
        const char *name = d->names.constData() + e.nameOff;
        if (!test(d->folders.at(e.folder), name, e.nameLen, e.isDir))
            continue;
        ++r.total;
        if (found.size() < kSortCap)
            found << Match{&d->folders.at(e.folder), name, e.nameLen, bool(e.isDir), e.size, e.mtime};
    }
    if (o) {
        for (auto it = o->items.cbegin(); it != o->items.cend(); ++it) {
            for (const OverlayItem &x : it.value()) {
                if (!test(it.key(), x.name.constData(), int(x.name.size()), x.isDir))
                    continue;
                ++r.total;
                if (found.size() < kSortCap)
                    found << Match{&it.key(), x.name.constData(), int(x.name.size()), x.isDir, x.size, x.mtime};
            }
        }
    }
    if (cancel && *cancel)
        return {};
    std::sort(found.begin(), found.end(), [](const Match &a, const Match &b) {
        const int n = qMin(a.nameLen, b.nameLen);
        for (int i = 0; i < n; ++i) {
            const uchar x = lower(uchar(a.name[i]));
            const uchar y = lower(uchar(b.name[i]));
            if (x != y)
                return x < y;
        }
        if (a.nameLen != b.nameLen)
            return a.nameLen < b.nameLen;
        return *a.folder < *b.folder;
    });
    r.truncated = r.total > limit || found.size() < r.total;
    const int n = qMin(int(found.size()), limit);
    r.hits.reserve(n);
    for (int i = 0; i < n; ++i) {
        const Match &m = found.at(i);
        Hit h;
        h.name = QString::fromUtf8(m.name, m.nameLen);
        h.folder = QFile::decodeName(*m.folder);
        h.isDir = m.isDir;
        h.size = m.size;
        h.modified = QDateTime::fromSecsSinceEpoch(m.mtime);
        r.hits << h;
    }
    return r;
}
