#include "core/Job.h"

#include "core/Archive.h"

#include "core/FileOps.h"
#include "core/Location.h"
#include "core/Trash.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QThread>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

namespace {

constexpr qint64 kChunk = 1024 * 1024;
const QString kPartSuffix = QStringLiteral(".linopus-part");

QString errnoText()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

QByteArray enc(const QString &p)
{
    return QFile::encodeName(p);
}

bool existsNoFollow(const QString &p)
{
    struct stat st;
    return ::lstat(enc(p).constData(), &st) == 0;
}

bool isRealDir(const QString &p)
{
    struct stat st;
    return ::lstat(enc(p).constData(), &st) == 0 && S_ISDIR(st.st_mode);
}

QString itemsWord(int n)
{
    return n == 1 ? QStringLiteral("1 item") : QStringLiteral("%1 items").arg(n);
}

} // namespace

JobBase::JobBase(int id, JobSpec spec, QString queueKey)
    : m_id(id)
    , m_spec(std::move(spec))
    , m_queueKey(std::move(queueKey))
{
}

JobBase::~JobBase() = default;

void JobBase::fillCommon(JobSnapshot &s) const
{
    s.id = m_id;
    s.type = m_spec.type;
    s.title = describe(m_spec);
    s.queueKey = m_queueKey;
    if (!m_spec.sources.isEmpty())
        s.fromFolder = Location::display(Location::parent(m_spec.sources.first()));
    s.toFolder = Location::display(m_spec.destDir);
}

Job::Job(int id, JobSpec spec, QString queueKey)
    : JobBase(id, std::move(spec), std::move(queueKey))
{
}

Job::~Job()
{
    waitFinished();
}

void Job::start(const CollisionNotifier &notifier, const DoneCallback &done)
{
    m_thread = QThread::create([this, notifier, done]() {
        run(notifier);
        done();
    });
    m_thread->start();
}

void Job::waitFinished()
{
    if (!m_thread)
        return;
    m_thread->wait();
    delete m_thread;
    m_thread = nullptr;
}

QString JobBase::describe(const JobSpec &spec)
{
    const int n = static_cast<int>(spec.sources.size());
    const QString what = n == 1 ? QStringLiteral("\"%1\"").arg(Location::fileName(spec.sources.first())) : itemsWord(n);
    switch (spec.type) {
    case JobType::Copy:
        return QStringLiteral("Copying %1 to %2").arg(what, Location::title(spec.destDir));
    case JobType::Move:
        return QStringLiteral("Moving %1 to %2").arg(what, Location::title(spec.destDir));
    case JobType::Trash:
        return QStringLiteral("Moving %1 to the trash").arg(what);
    case JobType::Delete:
        return QStringLiteral("Deleting %1 permanently").arg(what);
    case JobType::Pack:
        return QStringLiteral("Packing %1 into %2").arg(what, spec.archiveName);
    case JobType::Unpack:
        return QStringLiteral("Extracting %1 to %2").arg(what, Location::title(spec.destDir));
    }
    return {};
}

JobSnapshot Job::snapshot() const
{
    QMutexLocker lock(&m_mutex);
    JobSnapshot s;
    fillCommon(s);
    s.state = m_state;
    s.currentItem = m_current;
    s.currentSize = m_currentSize;
    s.currentDone = m_currentDone;
    s.totalBytes = m_totalBytes;
    s.doneBytes = m_doneBytes;
    s.totalItems = m_totalItems;
    s.doneItems = m_doneItems;
    s.errors = m_errors;
    s.bypassQueue = m_bypassQueue;
    if (m_runClock.isValid()) {
        qint64 ms = m_runClock.elapsed() - m_pausedMs;
        if (m_paused && m_pauseClock.isValid())
            ms -= m_pauseClock.elapsed();
        if (ms > 250)
            s.bytesPerSecond = static_cast<double>(m_doneBytes) * 1000.0 / static_cast<double>(ms);
    }
    return s;
}

void Job::setState(JobState s)
{
    QMutexLocker lock(&m_mutex);
    if (m_paused && (s == JobState::Running || s == JobState::Scanning)) {
        m_stateBeforePause = s;
        return;
    }
    m_state = s;
}

void Job::pause()
{
    QMutexLocker lock(&m_mutex);
    if (m_paused || m_state == JobState::Finished || m_state == JobState::Failed || m_state == JobState::Canceled)
        return;
    m_paused = true;
    if (m_state == JobState::Running || m_state == JobState::Scanning) {
        m_stateBeforePause = m_state;
        m_state = JobState::Paused;
    } else if (m_state == JobState::Queued) {
        m_stateBeforePause = JobState::Running;
        m_state = JobState::Paused;
    }
    m_pauseClock.start();
}

void Job::resume()
{
    QMutexLocker lock(&m_mutex);
    if (!m_paused)
        return;
    m_paused = false;
    if (m_pauseClock.isValid() && m_runClock.isValid())
        m_pausedMs += m_pauseClock.elapsed();
    m_pauseClock.invalidate();
    if (m_state == JobState::Paused)
        m_state = m_runClock.isValid() ? m_stateBeforePause : JobState::Queued;
    m_wake.wakeAll();
}

void Job::cancel()
{
    m_canceled = true;
    QMutexLocker lock(&m_mutex);
    if (m_state == JobState::Queued || (m_state == JobState::Paused && !m_runClock.isValid()))
        m_state = JobState::Canceled;
    m_wake.wakeAll();
}

void Job::setBypassQueue(bool on)
{
    QMutexLocker lock(&m_mutex);
    m_bypassQueue = on;
}

void Job::provideDecision(const CollisionDecision &decision)
{
    QMutexLocker lock(&m_mutex);
    m_decision = decision;
    m_hasDecision = true;
    m_wake.wakeAll();
}

bool Job::checkpoint()
{
    QMutexLocker lock(&m_mutex);
    while (m_paused && !m_canceled)
        m_wake.wait(&m_mutex);
    return !m_canceled;
}

QList<JobOutcome> Job::outcomes() const
{
    QMutexLocker lock(&m_mutex);
    return m_outcomes;
}

void Job::addOutcome(const QString &source, const QString &target, bool replaced)
{
    QMutexLocker lock(&m_mutex);
    m_outcomes.append(JobOutcome{source, target, replaced});
}

void Job::addError(const QString &message)
{
    QMutexLocker lock(&m_mutex);
    m_errors << message;
}

void Job::setCurrent(const QString &item, qint64 size)
{
    QMutexLocker lock(&m_mutex);
    m_current = item;
    m_currentSize = size;
    m_currentDone = 0;
}

void Job::addDone(qint64 bytes, int items)
{
    QMutexLocker lock(&m_mutex);
    m_doneBytes += bytes;
    m_currentDone += bytes;
    m_doneItems += items;
}

CollisionDecision Job::askCollision(const CollisionRequest &req)
{
    if (m_hasStickyDecision)
        return m_stickyDecision;
    if (m_spec.whenExists != CollisionAction::Ask) {
        CollisionDecision d;
        d.action = m_spec.whenExists;
        return d;
    }

    JobState previous;
    {
        QMutexLocker lock(&m_mutex);
        previous = m_state;
        m_state = JobState::WaitingForUser;
        m_hasDecision = false;
    }
    if (m_notifier)
        m_notifier(req);

    CollisionDecision d;
    {
        QMutexLocker lock(&m_mutex);
        while (!m_hasDecision && !m_canceled)
            m_wake.wait(&m_mutex);
        if (m_canceled) {
            d.action = CollisionAction::CancelJob;
        } else {
            d = m_decision;
        }
        if (m_state == JobState::WaitingForUser)
            m_state = m_paused ? JobState::Paused : previous;
    }
    if (d.action == CollisionAction::CancelJob)
        m_canceled = true;
    if (d.applyToAll && d.action != CollisionAction::CancelJob) {
        m_hasStickyDecision = true;
        m_stickyDecision = d;
        m_stickyDecision.newName.clear(); // each later Rename picks its own free name
    }
    return d;
}

bool Job::scan(const QString &path, const QString &name, int parent, std::vector<Entry> &out)
{
    if (m_canceled)
        return false;
    struct stat st;
    if (::lstat(enc(path).constData(), &st) != 0) {
        addError(QStringLiteral("%1: %2").arg(path, errnoText()));
        return true;
    }
    Entry e;
    e.src = path;
    e.name = name;
    e.parent = parent;
    e.isSymlink = S_ISLNK(st.st_mode);
    e.isDir = S_ISDIR(st.st_mode);
    if (!e.isDir && !e.isSymlink && !S_ISREG(st.st_mode)) {
        addError(QStringLiteral("%1: skipped (device, socket or pipe)").arg(path));
        return true;
    }
    e.size = (e.isDir || e.isSymlink) ? 0 : static_cast<qint64>(st.st_size);
    out.push_back(e);
    const int self = static_cast<int>(out.size()) - 1;
    {
        QMutexLocker lock(&m_mutex);
        m_totalBytes += e.size;
        m_totalItems += 1;
    }
    if (e.isDir) {
        const QDir dir(path);
        if (!dir.isReadable()) {
            addError(QStringLiteral("%1: cannot read folder").arg(path));
            return true;
        }
        const QStringList children = dir.entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                                                   QDir::Name);
        for (const QString &child : children) {
            if (!scan(path + QLatin1Char('/') + child, child, self, out))
                return false;
        }
    }
    return true;
}

void Job::run(const CollisionNotifier &notifier)
{
    m_notifier = notifier;
    {
        QMutexLocker lock(&m_mutex);
        if (m_canceled) {
            m_state = JobState::Canceled;
            return;
        }
        m_runClock.start();
        if (m_paused)
            m_pauseClock.start();
    }
    if (!checkpoint()) {
        setState(JobState::Canceled);
        return;
    }

    switch (m_spec.type) {
    case JobType::Copy:
    case JobType::Move:
        runCopyMove();
        break;
    case JobType::Trash:
        runTrash();
        break;
    case JobType::Delete:
        runDelete();
        break;
    case JobType::Pack:
        runPack();
        break;
    case JobType::Unpack:
        runUnpack();
        break;
    }

    QMutexLocker lock(&m_mutex);
    m_current.clear();
    m_paused = false;
    if (m_canceled)
        m_state = JobState::Canceled;
    else
        m_state = m_errors.isEmpty() ? JobState::Finished : JobState::Failed;
}

void Job::runCopyMove()
{
    const bool move = m_spec.type == JobType::Move;
    const QString dest = QDir::cleanPath(m_spec.destDir);

    if (!isRealDir(dest)) {
        addError(QStringLiteral("The destination \"%1\" is not a folder.").arg(dest));
        return;
    }

    setState(JobState::Scanning);
    std::vector<Entry> entries;
    std::vector<QString> quickMoves; // same-device moves done with a single rename

    for (const QString &rawSource : m_spec.sources) {
        const QString source = QDir::cleanPath(rawSource);
        const QString name = m_spec.targetNames.value(source, QFileInfo(source).fileName());
        if (isRealDir(source) && FileOps::isSameOrInside(dest, source)) {
            addError(QStringLiteral("Cannot %1 \"%2\" into itself.").arg(move ? QStringLiteral("move") : QStringLiteral("copy"), name));
            continue;
        }
        if (move && QFileInfo(source).absolutePath() == dest && name == QFileInfo(source).fileName())
            continue; // already there
        if (move && FileOps::sameDevice(source, dest) && !existsNoFollow(dest + QLatin1Char('/') + name)) {
            quickMoves.push_back(source);
            QMutexLocker lock(&m_mutex);
            m_totalItems += 1;
            continue;
        }
        if (!scan(source, name, -1, entries))
            break;
    }
    if (m_canceled)
        return;
    setState(JobState::Running);

    // Whole items moved within one filesystem: one rename each, instant.
    for (const QString &source : quickMoves) {
        if (!checkpoint())
            return;
        const QString name = m_spec.targetNames.value(source, QFileInfo(source).fileName());
        const QString target = dest + QLatin1Char('/') + name;
        setCurrent(name);
        if (existsNoFollow(target)) {
            // Appeared after the scan; fall back to the full path through the queue.
            scan(source, name, -1, entries);
            addDone(0, 1);
            continue;
        }
        if (::rename(enc(source).constData(), enc(target).constData()) != 0)
            addError(QStringLiteral("%1: %2").arg(name, errnoText()));
        else
            addOutcome(source, target);
        addDone(0, 1);
    }

    std::vector<QString> targetPath(entries.size());
    std::vector<char> skipped(entries.size(), 0);
    std::vector<char> incomplete(entries.size(), 0); // folder kept because something inside stayed
    std::vector<char> created(entries.size(), 0);    // made by this job (not merged into)
    // Undo works on the highest level this job made.
    auto record = [&](size_t i, const QString &target, bool replaced) {
        created[i] = 1;
        const int p = entries[i].parent;
        if (p < 0 || !created[static_cast<size_t>(p)])
            addOutcome(entries[i].src, target, replaced);
    };
    std::vector<std::pair<QString, QDateTime>> dirTimes;

    for (size_t i = 0; i < entries.size(); ++i) {
        const Entry &e = entries[i];
        if (!checkpoint())
            return;

        auto markIncomplete = [&](int idx) {
            while (idx >= 0) {
                incomplete[static_cast<size_t>(idx)] = 1;
                idx = entries[static_cast<size_t>(idx)].parent;
            }
        };

        if (e.parent >= 0 && skipped[static_cast<size_t>(e.parent)]) {
            skipped[i] = 1;
            addDone(e.size, 1);
            continue;
        }
        const QString parentTarget = e.parent < 0 ? dest : targetPath[static_cast<size_t>(e.parent)];
        QString target = parentTarget + QLatin1Char('/') + e.name;
        setCurrent(e.src, e.size);

        // Copying into the folder the item already lives in: pick a free name.
        if (target == e.src && !move)
            target = parentTarget + QLatin1Char('/') + FileOps::uniqueName(parentTarget, e.name);

        bool replacing = false;
        if (existsNoFollow(target)) {
            const bool targetIsDir = isRealDir(target);
            if (e.isDir && targetIsDir) {
                targetPath[i] = target; // merge into the existing folder
                addDone(0, 1);
                continue;
            }
            CollisionRequest req;
            req.jobId = m_id;
            req.source = e.src;
            req.target = target;
            req.suggestedName = FileOps::uniqueName(parentTarget, e.name);
            req.sourceIsDir = e.isDir;
            req.targetIsDir = targetIsDir;
            const CollisionDecision d = askCollision(req);

            CollisionAction action = d.action;
            if (action == CollisionAction::ReplaceIfNewer) {
                const QDateTime srcTime = QFileInfo(e.src).lastModified();
                const QDateTime dstTime = QFileInfo(target).lastModified();
                action = srcTime > dstTime ? CollisionAction::Replace : CollisionAction::Skip;
            }
            if (action == CollisionAction::Replace && targetIsDir) {
                addError(QStringLiteral("%1: a folder with that name is in the way; not replaced").arg(e.name));
                action = CollisionAction::Skip;
            }
            switch (action) {
            case CollisionAction::CancelJob:
                return;
            case CollisionAction::Skip:
                skipped[i] = 1;
                markIncomplete(e.parent);
                addDone(e.size, 1);
                continue;
            case CollisionAction::Rename: {
                QString newName = d.newName.trimmed();
                QString why;
                if (newName.isEmpty() || !FileOps::isValidFileName(newName, &why))
                    newName = req.suggestedName;
                if (existsNoFollow(parentTarget + QLatin1Char('/') + newName))
                    newName = FileOps::uniqueName(parentTarget, newName);
                target = parentTarget + QLatin1Char('/') + newName;
                break;
            }
            case CollisionAction::Replace:
                replacing = true;
                break;
            default:
                break;
            }
        }

        if (e.isDir) {
            struct stat st;
            mode_t mode = 0755;
            if (::lstat(enc(e.src).constData(), &st) == 0)
                mode = (st.st_mode & 07777) | S_IRWXU;
            if (replacing && ::unlink(enc(target).constData()) != 0) {
                addError(QStringLiteral("%1: %2").arg(target, errnoText()));
                skipped[i] = 1;
                markIncomplete(e.parent);
                continue;
            }
            if (::mkdir(enc(target).constData(), mode) != 0) {
                if (errno != EEXIST) {
                    addError(QStringLiteral("%1: %2").arg(target, errnoText()));
                    skipped[i] = 1;
                    markIncomplete(e.parent);
                    continue;
                }
            } else {
                record(i, target, false);
            }
            targetPath[i] = target;
            dirTimes.emplace_back(target, QFileInfo(e.src).lastModified());
            addDone(0, 1);
            continue;
        }

        QString error;
        bool ok = false;
        if (move && FileOps::sameDevice(e.src, parentTarget)) {
            ok = ::rename(enc(e.src).constData(), enc(target).constData()) == 0;
            if (!ok)
                error = errnoText();
            addDone(e.size, 0);
        } else if (e.isSymlink) {
            if (replacing)
                ::unlink(enc(target).constData());
            ok = copySymlink(e.src, target, &error);
            addDone(0, 0);
        } else {
            ok = copyFile(e.src, target, &error);
        }
        if (m_canceled)
            return;
        if (!ok) {
            addError(QStringLiteral("%1: %2").arg(e.src, error));
            markIncomplete(e.parent);
            addDone(0, 1);
            continue;
        }
        record(i, target, replacing);
        if (move && existsNoFollow(e.src) && ::unlink(enc(e.src).constData()) != 0) {
            addError(QStringLiteral("%1: copied, but the original could not be removed: %2").arg(e.src, errnoText()));
            markIncomplete(e.parent);
        }
        addDone(0, 1);
    }

    // Folder dates last, after their contents stopped touching them.
    for (const auto &[path, time] : dirTimes) {
        QFile f(path);
        if (f.open(QIODevice::ReadOnly))
            f.setFileTime(time, QFileDevice::FileModificationTime);
    }

    if (move) {
        for (size_t i = entries.size(); i-- > 0;) {
            const Entry &e = entries[i];
            if (e.isDir && !skipped[i] && !incomplete[i])
                ::rmdir(enc(e.src).constData());
        }
    }
}

bool Job::copyFile(const QString &src, const QString &dst, QString *error)
{
    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        *error = in.errorString();
        return false;
    }
    const QString part = dst + kPartSuffix;
    ::unlink(enc(part).constData());
    QFile out(part);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = out.errorString();
        return false;
    }

    QByteArray buffer;
    buffer.resize(kChunk);
    while (true) {
        if (!checkpoint()) {
            out.close();
            ::unlink(enc(part).constData());
            return false;
        }
        const qint64 n = in.read(buffer.data(), kChunk);
        if (n < 0) {
            *error = in.errorString();
            out.close();
            ::unlink(enc(part).constData());
            return false;
        }
        if (n == 0)
            break;
        qint64 written = 0;
        while (written < n) {
            const qint64 w = out.write(buffer.constData() + written, n - written);
            if (w <= 0) {
                *error = out.errorString();
                out.close();
                ::unlink(enc(part).constData());
                return false;
            }
            written += w;
        }
        addDone(n, 0);
    }

    // Flush before stamping the date, or the final write would bump it again.
    if (!out.flush()) {
        *error = out.errorString();
        out.close();
        ::unlink(enc(part).constData());
        return false;
    }
    const QFileInfo srcInfo(src);
    out.setPermissions(in.permissions());
    out.setFileTime(srcInfo.lastModified(), QFileDevice::FileModificationTime);
    out.close();

    if (::rename(enc(part).constData(), enc(dst).constData()) != 0) {
        *error = errnoText();
        ::unlink(enc(part).constData());
        return false;
    }
    return true;
}

bool Job::copySymlink(const QString &src, const QString &dst, QString *error)
{
    QByteArray target(4096, '\0');
    const ssize_t n = ::readlink(enc(src).constData(), target.data(), static_cast<size_t>(target.size()) - 1);
    if (n < 0) {
        *error = errnoText();
        return false;
    }
    target.truncate(static_cast<int>(n));
    if (::symlink(target.constData(), enc(dst).constData()) != 0) {
        *error = errnoText();
        return false;
    }
    return true;
}

void Job::runTrash()
{
    {
        QMutexLocker lock(&m_mutex);
        m_totalItems = static_cast<int>(m_spec.sources.size());
    }
    setState(JobState::Running);
    for (const QString &source : m_spec.sources) {
        if (!checkpoint())
            return;
        setCurrent(source);
        QString error;
        QString trashed;
        if (!Trash::moveToTrash(source, &trashed, &error))
            addError(QStringLiteral("%1: %2").arg(QFileInfo(source).fileName(), error));
        else
            addOutcome(QDir::cleanPath(source), trashed);
        addDone(0, 1);
    }
}

void Job::runDelete()
{
    setState(JobState::Scanning);
    std::vector<Entry> entries;
    for (const QString &source : m_spec.sources) {
        if (!scan(QDir::cleanPath(source), QFileInfo(source).fileName(), -1, entries))
            return;
    }
    {
        // Deleting moves no bytes; progress counts items.
        QMutexLocker lock(&m_mutex);
        m_totalBytes = 0;
    }
    setState(JobState::Running);

    std::vector<char> failed(entries.size(), 0);
    for (size_t i = entries.size(); i-- > 0;) {
        if (!checkpoint())
            return;
        const Entry &e = entries[i];
        setCurrent(e.src);
        if (failed[i]) {
            addDone(0, 1);
            continue; // something inside could not be deleted
        }
        const int rc = e.isDir ? ::rmdir(enc(e.src).constData()) : ::unlink(enc(e.src).constData());
        if (rc != 0 && errno != ENOENT) {
            addError(QStringLiteral("%1: %2").arg(e.src, errnoText()));
            for (int p = e.parent; p >= 0; p = entries[static_cast<size_t>(p)].parent)
                failed[static_cast<size_t>(p)] = 1;
        }
        addDone(0, 1);
    }
}

// ---- Archives ----------------------------------------------------------------

void Job::runPack()
{
    Archive::Format format;
    if (!Archive::formatFromKey(m_spec.archiveFormat, format) && !Archive::formatFromName(m_spec.archiveName, format)) {
        addError(QStringLiteral("Unknown archive format \"%1\".").arg(m_spec.archiveFormat));
        return;
    }
    const QString dest = QDir::cleanPath(m_spec.destDir);
    if (!isRealDir(dest)) {
        addError(QStringLiteral("The destination \"%1\" is not a folder.").arg(dest));
        return;
    }
    // Counting first, for progress.
    setState(JobState::Scanning);
    std::vector<Entry> entries;
    for (const QString &s : m_spec.sources) {
        if (!scan(QDir::cleanPath(s), QFileInfo(s).fileName(), -1, entries))
            return;
    }
    QString target = dest + QLatin1Char('/') + m_spec.archiveName;
    if (existsNoFollow(target)) {
        if (m_spec.whenExists == CollisionAction::Skip) {
            addError(QStringLiteral("%1 is already there.").arg(m_spec.archiveName));
            return;
        }
        if (m_spec.whenExists != CollisionAction::Replace)
            target = dest + QLatin1Char('/') + FileOps::uniqueName(dest, m_spec.archiveName);
    }
    setState(JobState::Running);
    // Written beside it under another name, swapped in when complete: a
    // canceled or failed archive never looks like a finished one.
    const QString part = target + QStringLiteral(".linopus-part");
    Archive::Hooks hooks;
    hooks.keepGoing = [this]() { return checkpoint(); };
    hooks.current = [this](const QString &item, qint64 size) {
        setCurrent(item, size);
        addDone(0, 1);
    };
    hooks.advanced = [this](qint64 bytes) { addDone(bytes, 0); };
    hooks.problem = [this](const QString &m) { addError(m); };
    QString error;
    const bool ok = Archive::pack(m_spec.sources, part, format, hooks, &error);
    if (!ok || m_canceled) {
        QFile::remove(part);
        if (!m_canceled && !error.isEmpty())
            addError(error);
        return;
    }
    if (existsNoFollow(target))
        ::unlink(enc(target).constData());
    if (::rename(enc(part).constData(), enc(target).constData()) != 0) {
        addError(QStringLiteral("%1: %2").arg(target, errnoText()));
        QFile::remove(part);
        return;
    }
    addOutcome(m_spec.sources.value(0), target);
}

void Job::runUnpack()
{
    const QString dest = QDir::cleanPath(m_spec.destDir);
    if (!isRealDir(dest)) {
        addError(QStringLiteral("The destination \"%1\" is not a folder.").arg(dest));
        return;
    }
    qint64 total = 0;
    for (const QString &s : m_spec.sources)
        total += QFileInfo(s).size();
    {
        QMutexLocker lock(&m_mutex);
        m_totalBytes = total;
        m_totalItems = 0; // entries are counted as they come
    }
    setState(JobState::Running);
    const bool replace = m_spec.whenExists == CollisionAction::Replace;
    for (const QString &raw : m_spec.sources) {
        if (!checkpoint())
            return;
        const QString archive = QDir::cleanPath(raw);
        const QString shown = QFileInfo(archive).fileName();
        QString error;
        const QStringList tops = Archive::topLevelNames(archive, &error);
        if (tops.isEmpty()) {
            addError(QStringLiteral("%1: %2").arg(shown, error.isEmpty() ? QStringLiteral("empty, or not an archive") : error));
            addDone(QFileInfo(archive).size(), 0);
            continue;
        }
        // Where it goes: one folder (or file) inside goes straight in;
        // anything else gets a folder named after the archive.
        QString into = dest;
        QMap<QString, QString> rename;
        const bool own = m_spec.extractMode == JobSpec::ExtractMode::Folder
            || (m_spec.extractMode == JobSpec::ExtractMode::Smart && tops.size() > 1);
        if (own) {
            QString folder = Archive::baseName(archive);
            if (existsNoFollow(dest + QLatin1Char('/') + folder) && !replace)
                folder = FileOps::uniqueName(dest, folder);
            into = dest + QLatin1Char('/') + folder;
            const bool existed = existsNoFollow(into);
            if (!QDir().mkpath(into)) {
                addError(QStringLiteral("%1: cannot make %2").arg(shown, into));
                continue;
            }
            if (!existed) // merged into a folder that was there: undo leaves it
                addOutcome(archive, into);
        } else if (!replace) {
            for (const QString &t : tops) {
                if (existsNoFollow(dest + QLatin1Char('/') + t))
                    rename.insert(t, FileOps::uniqueName(dest, t));
            }
        }
        Archive::Hooks hooks;
        hooks.keepGoing = [this]() { return checkpoint(); };
        hooks.current = [this](const QString &item, qint64 size) {
            {
                QMutexLocker lock(&m_mutex);
                ++m_totalItems;
            }
            setCurrent(item, size);
            addDone(0, 1);
        };
        hooks.advanced = [this](qint64 bytes) { addDone(bytes, 0); };
        hooks.problem = [this](const QString &m) { addError(m); };
        QStringList created;
        const bool ok = Archive::unpack(archive, into, rename, replace, hooks, &created, &error);
        if (!own) {
            for (const QString &c : std::as_const(created))
                addOutcome(archive, c);
        }
        if (m_canceled)
            return;
        if (!ok)
            addError(QStringLiteral("%1: %2").arg(shown, error));
    }
}
