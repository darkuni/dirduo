#include "core/FolderSync.h"

#include "core/CommandEngine.h"
#include "core/FileOps.h"
#include "core/Location.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

#include <sys/stat.h>

#include <algorithm>

// ---------------------------------------------------------------- hashing

QByteArray FileHash::of(const QString &path, qint64 limit, const std::atomic<bool> *cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash h(QCryptographicHash::Md5);
    QByteArray buf(256 * 1024, Qt::Uninitialized);
    qint64 left = limit < 0 ? f.size() : qMin(limit, f.size());
    while (left > 0) {
        if (cancel && cancel->load())
            return {};
        const qint64 n = f.read(buf.data(), qMin<qint64>(buf.size(), left));
        if (n <= 0)
            return {};
        h.addData(QByteArrayView(buf.constData(), n));
        left -= n;
    }
    return h.result();
}

// ---------------------------------------------------------------- options

QString SyncOptions::modeName(Mode m)
{
    switch (m) {
    case Mode::Mirror:
        return QStringLiteral("mirror");
    case Mode::TwoWay:
        return QStringLiteral("twoway");
    default:
        return QStringLiteral("update");
    }
}

bool SyncOptions::modeFromName(const QString &name, Mode &out)
{
    const QString n = name.toLower();
    if (n == QLatin1String("update") || n == QLatin1String("oneway"))
        out = Mode::Update;
    else if (n == QLatin1String("mirror"))
        out = Mode::Mirror;
    else if (n == QLatin1String("twoway") || n == QLatin1String("both"))
        out = Mode::TwoWay;
    else
        return false;
    return true;
}

QString SyncOptions::compareName(CompareBy c)
{
    switch (c) {
    case CompareBy::Date:
        return QStringLiteral("date");
    case CompareBy::Size:
        return QStringLiteral("size");
    case CompareBy::Contents:
        return QStringLiteral("contents");
    default:
        return QStringLiteral("datesize");
    }
}

bool SyncOptions::compareFromName(const QString &name, CompareBy &out)
{
    const QString n = name.toLower();
    if (n == QLatin1String("date"))
        out = CompareBy::Date;
    else if (n == QLatin1String("size"))
        out = CompareBy::Size;
    else if (n == QLatin1String("datesize") || n == QLatin1String("dateandsize"))
        out = CompareBy::DateAndSize;
    else if (n == QLatin1String("contents") || n == QLatin1String("md5"))
        out = CompareBy::Contents;
    else
        return false;
    return true;
}

// ---------------------------------------------------------------- compare

namespace {

QList<QRegularExpression> compileExcludes(const QStringList &patterns)
{
    QList<QRegularExpression> out;
    for (const QString &p : patterns) {
        for (const QString &part : p.split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
            const QString t = part.trimmed();
            if (!t.isEmpty())
                out << QRegularExpression(QRegularExpression::wildcardToRegularExpression(t),
                                          QRegularExpression::CaseInsensitiveOption);
        }
    }
    return out;
}

bool excluded(const QString &name, const QList<QRegularExpression> &ex)
{
    for (const QRegularExpression &re : ex) {
        if (re.match(name).hasMatch())
            return true;
    }
    return false;
}

QMap<QString, QFileInfo> listDir(const QString &dir, bool hidden, const QList<QRegularExpression> &ex)
{
    QMap<QString, QFileInfo> out;
    QDir::Filters f = QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::System;
    if (hidden)
        f |= QDir::Hidden;
    const QFileInfoList list = QDir(dir).entryInfoList(f, QDir::NoSort);
    for (const QFileInfo &fi : list) {
        if (!excluded(fi.fileName(), ex))
            out.insert(fi.fileName(), fi);
    }
    return out;
}

struct Comparer
{
    QString left;
    QString right;
    SyncOptions o;
    ScanProgress *progress;
    QList<QRegularExpression> ex;
    QList<SyncEntry> out;
    QCollator coll;

    bool cancelled() const { return progress && progress->cancel.load(); }

    void walk(const QString &rel)
    {
        if (cancelled())
            return;
        const QString l = rel.isEmpty() ? left : left + QLatin1Char('/') + rel;
        const QString r = rel.isEmpty() ? right : right + QLatin1Char('/') + rel;
        const QMap<QString, QFileInfo> ls = listDir(l, o.includeHidden, ex);
        const QMap<QString, QFileInfo> rs = listDir(r, o.includeHidden, ex);
        QStringList names = ls.keys();
        for (const QString &n : rs.keys()) {
            if (!ls.contains(n))
                names << n;
        }
        std::sort(names.begin(), names.end(), [this](const QString &a, const QString &b) { return coll.compare(a, b) < 0; });
        for (const QString &name : std::as_const(names)) {
            if (cancelled())
                return;
            const QString childRel = rel.isEmpty() ? name : rel + QLatin1Char('/') + name;
            const bool inL = ls.contains(name);
            const bool inR = rs.contains(name);
            const QFileInfo lf = ls.value(name);
            const QFileInfo rf = rs.value(name);
            // A symlink to a folder is treated as a file entry: never followed.
            const bool lDir = inL && lf.isDir() && !lf.isSymLink();
            const bool rDir = inR && rf.isDir() && !rf.isSymLink();
            if (progress)
                progress->files.fetch_add(1);
            if (lDir && rDir) {
                walk(childRel);
                continue;
            }
            SyncEntry e;
            e.rel = childRel;
            if (inL) {
                e.leftSize = lDir ? -1 : lf.size();
                e.leftTime = lf.lastModified();
            }
            if (inR) {
                e.rightSize = rDir ? -1 : rf.size();
                e.rightTime = rf.lastModified();
            }
            e.isDir = (inL && lDir) || (!inL && rDir);
            if (inL && !inR) {
                e.state = SyncEntry::State::OnlyLeft;
            } else if (inR && !inL) {
                e.state = SyncEntry::State::OnlyRight;
            } else if (lDir != rDir) {
                e.state = SyncEntry::State::Different; // a file one side, a folder the other
                e.isDir = true;
            } else {
                e.state = compareFiles(lf, rf);
            }
            e.action = (lDir != rDir && inL && inR) ? SyncEntry::Action::None : FolderSync::defaultAction(e.state, o.mode);
            out << e;
        }
    }

    SyncEntry::State byDate(const QFileInfo &lf, const QFileInfo &rf) const
    {
        const qint64 d = lf.lastModified().toSecsSinceEpoch() - rf.lastModified().toSecsSinceEpoch();
        if (d > o.toleranceSecs)
            return SyncEntry::State::LeftNewer;
        if (d < -o.toleranceSecs)
            return SyncEntry::State::RightNewer;
        return SyncEntry::State::Different;
    }

    SyncEntry::State compareFiles(const QFileInfo &lf, const QFileInfo &rf)
    {
        const qint64 d = qAbs(lf.lastModified().toSecsSinceEpoch() - rf.lastModified().toSecsSinceEpoch());
        const bool sameTime = d <= o.toleranceSecs;
        const bool sameSize = lf.size() == rf.size();
        switch (o.compareBy) {
        case SyncOptions::CompareBy::Date:
            return sameTime ? SyncEntry::State::Same : byDate(lf, rf);
        case SyncOptions::CompareBy::Size:
            return sameSize ? SyncEntry::State::Same : byDate(lf, rf);
        case SyncOptions::CompareBy::DateAndSize:
            return sameSize && sameTime ? SyncEntry::State::Same : byDate(lf, rf);
        case SyncOptions::CompareBy::Contents: {
            if (!sameSize)
                return byDate(lf, rf);
            if (progress)
                progress->phase.store(1);
            const QByteArray a = FileHash::of(lf.absoluteFilePath(), -1, progress ? &progress->cancel : nullptr);
            const QByteArray b = FileHash::of(rf.absoluteFilePath(), -1, progress ? &progress->cancel : nullptr);
            if (!a.isEmpty() && a == b)
                return SyncEntry::State::Same;
            return byDate(lf, rf);
        }
        }
        return SyncEntry::State::Same;
    }
};

} // namespace

QList<SyncEntry> FolderSync::compare(const QString &left, const QString &right, const SyncOptions &options, ScanProgress *progress)
{
    Comparer c{Location::clean(left), Location::clean(right), options, progress, compileExcludes(options.exclude), {}, {}};
    c.coll.setNumericMode(true);
    c.coll.setCaseSensitivity(Qt::CaseInsensitive);
    c.walk(QString());
    return c.out;
}

SyncEntry::Action FolderSync::defaultAction(SyncEntry::State s, SyncOptions::Mode m)
{
    using A = SyncEntry::Action;
    using S = SyncEntry::State;
    using M = SyncOptions::Mode;
    switch (s) {
    case S::OnlyLeft:
        return A::CopyToRight;
    case S::OnlyRight:
        return m == M::Mirror ? A::DeleteRight : m == M::TwoWay ? A::CopyToLeft : A::None;
    case S::LeftNewer:
        return A::CopyToRight;
    case S::RightNewer:
        // Update never overwrites something newer; Mirror makes the right a copy of the left.
        return m == M::Mirror ? A::CopyToRight : m == M::TwoWay ? A::CopyToLeft : A::None;
    case S::Different:
        // Same time, different file: in two-way there is no telling which wins.
        return m == M::TwoWay ? A::None : A::CopyToRight;
    case S::Same:
        return A::None;
    }
    return A::None;
}

QString FolderSync::stateText(SyncEntry::State s)
{
    switch (s) {
    case SyncEntry::State::OnlyLeft:
        return QStringLiteral("Only on the left");
    case SyncEntry::State::OnlyRight:
        return QStringLiteral("Only on the right");
    case SyncEntry::State::LeftNewer:
        return QStringLiteral("Left is newer");
    case SyncEntry::State::RightNewer:
        return QStringLiteral("Right is newer");
    case SyncEntry::State::Different:
        return QStringLiteral("Different");
    case SyncEntry::State::Same:
        return QStringLiteral("Same");
    }
    return {};
}

QString FolderSync::actionText(SyncEntry::Action a)
{
    switch (a) {
    case SyncEntry::Action::CopyToRight:
        return QStringLiteral("Copy ->");
    case SyncEntry::Action::CopyToLeft:
        return QStringLiteral("<- Copy");
    case SyncEntry::Action::DeleteRight:
        return QStringLiteral("Delete right");
    case SyncEntry::Action::DeleteLeft:
        return QStringLiteral("Delete left");
    case SyncEntry::Action::None:
        return QStringLiteral("Skip");
    }
    return {};
}

FolderSync::Plan FolderSync::plan(const QList<SyncEntry> &entries, const QString &rawLeft, const QString &rawRight)
{
    const QString left = Location::clean(rawLeft);
    const QString right = Location::clean(rawRight);
    Plan p;
    for (const SyncEntry &e : entries) {
        const QString l = left + QLatin1Char('/') + e.rel;
        const QString r = right + QLatin1Char('/') + e.rel;
        switch (e.action) {
        case SyncEntry::Action::CopyToRight:
            p.copies[Location::parent(r)] << l;
            p.bytes += qMax<qint64>(0, e.leftSize);
            break;
        case SyncEntry::Action::CopyToLeft:
            p.copies[Location::parent(l)] << r;
            p.bytes += qMax<qint64>(0, e.rightSize);
            break;
        case SyncEntry::Action::DeleteRight:
            p.deletes << r;
            break;
        case SyncEntry::Action::DeleteLeft:
            p.deletes << l;
            break;
        case SyncEntry::Action::None:
            continue;
        }
        ++p.items;
    }
    return p;
}

int FolderSync::run(const Plan &p, CommandEngine &engine)
{
    int jobs = 0;
    for (auto it = p.copies.cbegin(); it != p.copies.cend(); ++it) {
        QDir().mkpath(it.key()); // a new subfolder on the other side
        if (engine.copyItems(it.value(), it.key(), false, CollisionAction::Replace))
            ++jobs;
    }
    if (!p.deletes.isEmpty() && engine.trashItems(p.deletes))
        ++jobs;
    return jobs;
}

// ---------------------------------------------------------------- duplicates

QList<DuplicateGroup> Duplicates::find(const QStringList &roots, const DuplicateOptions &o, ScanProgress *progress)
{
    const QList<QRegularExpression> ex = compileExcludes(o.exclude);
    auto cancelled = [progress]() { return progress && progress->cancel.load(); };

    // Every file once, even when roots overlap or files are hard links.
    QSet<QPair<quint64, quint64>> seenInodes;
    QHash<qint64, QStringList> bySize;
    QHash<QString, QStringList> byName;
    for (const QString &root : roots) {
        QDir::Filters f = QDir::Files | QDir::NoSymLinks;
        if (o.includeHidden)
            f |= QDir::Hidden;
        QDirIterator it(Location::clean(root), f, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancelled())
                return {};
            const QString path = it.next();
            const QFileInfo fi = it.fileInfo();
            if (excluded(fi.fileName(), ex))
                continue;
            // Skip anything inside an excluded or hidden folder.
            if (!o.includeHidden && path.contains(QLatin1String("/.")))
                continue;
            if (fi.size() < o.minSize)
                continue;
            struct stat st;
            if (::stat(QFile::encodeName(path).constData(), &st) == 0) {
                const auto key = qMakePair(quint64(st.st_dev), quint64(st.st_ino));
                if (seenInodes.contains(key))
                    continue;
                seenInodes.insert(key);
            }
            if (progress)
                progress->files.fetch_add(1);
            if (o.by == DuplicateOptions::By::Name)
                byName[fi.fileName().toLower()] << path;
            else if (o.by == DuplicateOptions::By::NameAndSize)
                byName[fi.fileName().toLower() + QLatin1Char('/') + QString::number(fi.size())] << path;
            else
                bySize[fi.size()] << path;
        }
    }

    QList<DuplicateGroup> out;
    auto add = [&out](const QStringList &paths) {
        if (paths.size() < 2)
            return;
        DuplicateGroup g;
        g.paths = paths;
        g.paths.sort();
        g.size = QFileInfo(g.paths.first()).size();
        out << g;
    };
    if (o.by != DuplicateOptions::By::Contents) {
        for (const QStringList &paths : std::as_const(byName))
            add(paths);
    } else {
        if (progress)
            progress->phase.store(1);
        for (auto it = bySize.cbegin(); it != bySize.cend(); ++it) {
            if (it.value().size() < 2)
                continue;
            // The first 64 KB weeds out most candidates cheaply; then the whole file.
            QHash<QByteArray, QStringList> byHead;
            for (const QString &p : it.value()) {
                if (cancelled())
                    return {};
                const QByteArray h = FileHash::of(p, 64 * 1024, progress ? &progress->cancel : nullptr);
                if (!h.isEmpty())
                    byHead[h] << p;
            }
            for (const QStringList &same : std::as_const(byHead)) {
                if (same.size() < 2)
                    continue;
                if (it.key() <= 64 * 1024) {
                    add(same);
                    continue;
                }
                QHash<QByteArray, QStringList> byFull;
                for (const QString &p : same) {
                    if (cancelled())
                        return {};
                    const QByteArray h = FileHash::of(p, -1, progress ? &progress->cancel : nullptr);
                    if (!h.isEmpty())
                        byFull[h] << p;
                }
                for (const QStringList &dupes : std::as_const(byFull))
                    add(dupes);
            }
        }
    }
    // Biggest waste first.
    std::sort(out.begin(), out.end(), [](const DuplicateGroup &a, const DuplicateGroup &b) {
        const qint64 wa = a.size * (a.paths.size() - 1);
        const qint64 wb = b.size * (b.paths.size() - 1);
        return wa != wb ? wa > wb : a.paths.first() < b.paths.first();
    });
    return out;
}

qint64 Duplicates::reclaimable(const QList<DuplicateGroup> &groups)
{
    qint64 n = 0;
    for (const DuplicateGroup &g : groups)
        n += g.size * (g.paths.size() - 1);
    return n;
}
