#include "core/Archive.h"

#include "core/FileKind.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#ifdef LINOPUS_HAVE_LIBARCHIVE
#include <archive.h>
#include <archive_entry.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Archive {

namespace {

struct FormatInfo
{
    Format format;
    const char *key;
    const char *name;
};

const FormatInfo kFormats[] = {
    {Format::Zip, "zip", "Zip"},          {Format::SevenZip, "7z", "7-Zip"},     {Format::TarGz, "tar.gz", "tar.gz"},
    {Format::TarXz, "tar.xz", "tar.xz"},  {Format::TarZst, "tar.zst", "tar.zst"}, {Format::TarBz2, "tar.bz2", "tar.bz2"},
    {Format::Tar, "tar", "tar"},
};

// Longest first, so "tar.gz" wins over "gz".
const QStringList &extractableSuffixes()
{
    static const QStringList s = {QStringLiteral(".tar.gz"),  QStringLiteral(".tar.xz"),  QStringLiteral(".tar.bz2"),
                                  QStringLiteral(".tar.zst"), QStringLiteral(".tar.lz"),  QStringLiteral(".tar.lzma"),
                                  QStringLiteral(".tgz"),     QStringLiteral(".txz"),     QStringLiteral(".tbz2"),
                                  QStringLiteral(".tbz"),     QStringLiteral(".tzst"),    QStringLiteral(".zip"),
                                  QStringLiteral(".7z"),      QStringLiteral(".rar"),     QStringLiteral(".tar"),
                                  QStringLiteral(".iso"),     QStringLiteral(".lha"),     QStringLiteral(".lzh"),
                                  QStringLiteral(".cab"),     QStringLiteral(".cpio"),    QStringLiteral(".xar"),
                                  QStringLiteral(".jar"),     QStringLiteral(".apk"),     QStringLiteral(".cbz"),
                                  QStringLiteral(".cbr"),     QStringLiteral(".cb7")};
    return s;
}

#ifdef LINOPUS_HAVE_LIBARCHIVE
QString entryName(struct archive_entry *e)
{
    if (const char *u = archive_entry_pathname_utf8(e))
        return QString::fromUtf8(u);
    if (const char *p = archive_entry_pathname(e))
        return QString::fromLocal8Bit(p);
    return {};
}

QString cleanEntry(QString n)
{
    while (n.startsWith(QLatin1String("./")))
        n.remove(0, 2);
    while (n.endsWith(QLatin1Char('/')))
        n.chop(1);
    return n;
}

QString errorOf(struct archive *a)
{
    const char *s = archive_error_string(a);
    const QString e = s ? QString::fromLocal8Bit(s) : QStringLiteral("unknown error");
    // libarchive says "Passphrase required" or "Incorrect passphrase".
    if (e.contains(QLatin1String("ncorrect passphrase"), Qt::CaseInsensitive))
        return QStringLiteral("The password is wrong.");
    if (e.contains(QLatin1String("assphrase"), Qt::CaseInsensitive) || e.contains(QLatin1String("encrypted"), Qt::CaseInsensitive))
        return QStringLiteral("It is protected by a password.");
    return e;
}

struct archive *openReader(const QString &path, const QString &password, int block, QString *error)
{
    struct archive *a = archive_read_new();
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);
    if (!password.isEmpty())
        archive_read_add_passphrase(a, password.toUtf8().constData());
    if (archive_read_open_filename(a, QFile::encodeName(path).constData(), block) != ARCHIVE_OK) {
        if (error)
            *error = errorOf(a);
        archive_read_free(a);
        return nullptr;
    }
    return a;
}
#endif

} // namespace

bool available()
{
#ifdef LINOPUS_HAVE_LIBARCHIVE
    return true;
#else
    return false;
#endif
}

QList<Format> formats()
{
    QList<Format> out;
    for (const FormatInfo &f : kFormats)
        out << f.format;
    return out;
}

bool formatFromKey(const QString &key, Format &out)
{
    const QString k = key.toLower().remove(QLatin1Char('.'), Qt::CaseSensitive);
    for (const FormatInfo &f : kFormats) {
        if (QString::fromLatin1(f.key).remove(QLatin1Char('.')) == k) {
            out = f.format;
            return true;
        }
    }
    if (k == QLatin1String("tgz")) { out = Format::TarGz; return true; }
    if (k == QLatin1String("txz")) { out = Format::TarXz; return true; }
    if (k == QLatin1String("7zip")) { out = Format::SevenZip; return true; }
    return false;
}

bool formatFromName(const QString &fileName, Format &out)
{
    const QString n = fileName.toLower();
    // The compressed tars come before plain tar in kFormats, so they win.
    for (const FormatInfo &f : kFormats) {
        if (n.endsWith(QLatin1Char('.') + QLatin1String(f.key))) {
            out = f.format;
            return true;
        }
    }
    if (n.endsWith(QLatin1String(".tgz"))) { out = Format::TarGz; return true; }
    if (n.endsWith(QLatin1String(".txz"))) { out = Format::TarXz; return true; }
    return false;
}

QString extensionFor(Format f)
{
    for (const FormatInfo &i : kFormats) {
        if (i.format == f)
            return QString::fromLatin1(i.key);
    }
    return {};
}

QString displayName(Format f)
{
    for (const FormatInfo &i : kFormats) {
        if (i.format == f)
            return QString::fromLatin1(i.name);
    }
    return {};
}

bool isArchive(const QString &path)
{
    const QString n = QFileInfo(path).fileName().toLower();
    for (const QString &s : extractableSuffixes()) {
        if (n.endsWith(s))
            return true;
    }
    return false;
}

QString baseName(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    const QString lower = name.toLower();
    for (const QString &s : extractableSuffixes()) {
        if (lower.endsWith(s) && lower.size() > s.size())
            return name.left(name.size() - s.size());
    }
    return QFileInfo(path).completeBaseName();
}

bool list(const QString &archivePath, QList<Entry> *entries, QString *error, int max)
{
#ifdef LINOPUS_HAVE_LIBARCHIVE
    struct archive *a = openReader(archivePath, {}, 64 * 1024, error);
    if (!a)
        return false;
    struct archive_entry *e = nullptr;
    int r;
    while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
        if (entries->size() >= max)
            break;
        Entry en;
        en.name = cleanEntry(entryName(e));
        en.isDir = archive_entry_filetype(e) == AE_IFDIR;
        en.size = archive_entry_size_is_set(e) ? archive_entry_size(e) : 0;
        en.encrypted = archive_entry_is_encrypted(e);
        if (!en.name.isEmpty())
            entries->append(en);
        archive_read_data_skip(a);
    }
    const bool ok = r == ARCHIVE_EOF || entries->size() >= max || !entries->isEmpty();
    if (!ok && error)
        *error = errorOf(a);
    archive_read_free(a);
    return ok;
#else
    Q_UNUSED(archivePath) Q_UNUSED(entries) Q_UNUSED(max)
    if (error)
        *error = QStringLiteral("This build of Linopus has no archive support.");
    return false;
#endif
}

bool canEncrypt(Format f)
{
    return f == Format::Zip;
}

bool isEncrypted(const QString &archivePath)
{
#ifdef LINOPUS_HAVE_LIBARCHIVE
    QString error;
    struct archive *a = openReader(archivePath, {}, 64 * 1024, &error);
    if (!a)
        return error == QLatin1String("It is protected by a password.");
    bool encrypted = false;
    struct archive_entry *e = nullptr;
    int r;
    int seen = 0;
    while (!encrypted && seen++ < 500 && ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN)) {
        encrypted = archive_entry_is_encrypted(e) || archive_read_has_encrypted_entries(a) > 0;
        archive_read_data_skip(a);
    }
    if (!encrypted && seen <= 500 && r < ARCHIVE_WARN)
        encrypted = errorOf(a) == QLatin1String("It is protected by a password.");
    archive_read_free(a);
    return encrypted;
#else
    Q_UNUSED(archivePath)
    return false;
#endif
}

QStringList topLevelNames(const QString &archivePath, QString *error, const QString &password)
{
    QStringList out;
#ifdef LINOPUS_HAVE_LIBARCHIVE
    struct archive *a = openReader(archivePath, password, 64 * 1024, error);
    if (!a)
        return out;
    QSet<QString> seen;
    struct archive_entry *e = nullptr;
    int r;
    while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
        const QString n = cleanEntry(entryName(e));
        const QString top = n.section(QLatin1Char('/'), 0, 0);
        if (!top.isEmpty() && top != QLatin1String("..") && !seen.contains(top)) {
            seen.insert(top);
            out << top;
        }
        archive_read_data_skip(a);
    }
    if (r != ARCHIVE_EOF && error)
        *error = errorOf(a);
    archive_read_free(a);
#else
    Q_UNUSED(archivePath) Q_UNUSED(password)
    if (error)
        *error = QStringLiteral("This build of Linopus has no archive support.");
#endif
    return out;
}

bool pack(const QStringList &sources, const QString &path, Format format, const Hooks &hooks, QString *error)
{
#ifdef LINOPUS_HAVE_LIBARCHIVE
    if (!hooks.password.isEmpty() && !canEncrypt(format)) {
        if (error)
            *error = QStringLiteral("Only zip archives can have a password.");
        return false;
    }
    struct archive *a = archive_write_new();
    switch (format) {
    case Format::Zip:
        archive_write_set_format_zip(a);
        archive_write_set_options(a, "zip:hdrcharset=UTF-8");
        if (!hooks.password.isEmpty()) {
            if (archive_write_set_options(a, "zip:encryption=aes256") != ARCHIVE_OK
                || archive_write_set_passphrase(a, hooks.password.toUtf8().constData()) != ARCHIVE_OK) {
                if (error)
                    *error = QStringLiteral("This build cannot encrypt zip files: %1").arg(errorOf(a));
                archive_write_free(a);
                return false;
            }
        }
        break;
    case Format::SevenZip:
        archive_write_set_format_7zip(a);
        break;
    default:
        archive_write_set_format_pax_restricted(a);
        break;
    }
    switch (format) {
    case Format::TarGz: archive_write_add_filter_gzip(a); break;
    case Format::TarXz: archive_write_add_filter_xz(a); break;
    case Format::TarBz2: archive_write_add_filter_bzip2(a); break;
    case Format::TarZst: archive_write_add_filter_zstd(a); break;
    default: break;
    }
    if (archive_write_open_filename(a, QFile::encodeName(path).constData()) != ARCHIVE_OK) {
        if (error)
            *error = errorOf(a);
        archive_write_free(a);
        return false;
    }
    bool ok = true;
    bool stopped = false;
    QByteArray buffer(256 * 1024, Qt::Uninitialized);

    auto addOne = [&](const QString &file, const QString &name) {
        struct stat st;
        const QByteArray enc = QFile::encodeName(file);
        if (::lstat(enc.constData(), &st) != 0) {
            if (hooks.problem)
                hooks.problem(QStringLiteral("%1: cannot be read").arg(name));
            return;
        }
        struct archive_entry *e = archive_entry_new();
        archive_entry_copy_stat(e, &st);
        archive_entry_set_pathname_utf8(e, name.toUtf8().constData());
        if (S_ISLNK(st.st_mode)) {
            // The link itself, as written (relative links stay relative).
            char link[4096];
            const ssize_t len = ::readlink(enc.constData(), link, sizeof(link) - 1);
            link[len >= 0 ? len : 0] = 0;
            archive_entry_set_symlink(e, link);
            archive_entry_set_size(e, 0);
        } else if (!S_ISREG(st.st_mode)) {
            archive_entry_set_size(e, 0);
        }
        if (hooks.current)
            hooks.current(file, S_ISREG(st.st_mode) ? st.st_size : 0);
        if (archive_write_header(a, e) < ARCHIVE_WARN) {
            if (hooks.problem)
                hooks.problem(QStringLiteral("%1: %2").arg(name, errorOf(a)));
            archive_entry_free(e);
            return;
        }
        if (S_ISREG(st.st_mode)) {
            const int fd = ::open(enc.constData(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) {
                if (hooks.problem)
                    hooks.problem(QStringLiteral("%1: cannot be opened").arg(name));
            } else {
                ssize_t got;
                while ((got = ::read(fd, buffer.data(), buffer.size())) > 0) {
                    if (hooks.keepGoing && !hooks.keepGoing()) {
                        stopped = true;
                        break;
                    }
                    if (archive_write_data(a, buffer.constData(), size_t(got)) < 0) {
                        if (hooks.problem)
                            hooks.problem(QStringLiteral("%1: %2").arg(name, errorOf(a)));
                        ok = false;
                        break;
                    }
                    if (hooks.advanced)
                        hooks.advanced(got);
                }
                ::close(fd);
            }
        }
        archive_entry_free(e);
    };

    for (const QString &raw : sources) {
        if (stopped || !ok)
            break;
        const QString src = QDir::cleanPath(raw);
        const QFileInfo fi(src);
        const QString base = fi.fileName();
        addOne(src, base);
        if (fi.isDir() && !fi.isSymLink()) {
            QDirIterator it(src, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
                            QDirIterator::Subdirectories);
            while (it.hasNext() && !stopped && ok) {
                const QString file = it.next();
                addOne(file, base + QLatin1Char('/') + QDir(src).relativeFilePath(file));
            }
        }
    }
    if (archive_write_close(a) != ARCHIVE_OK && ok && !stopped) {
        if (error)
            *error = errorOf(a);
        ok = false;
    }
    archive_write_free(a);
    if (stopped) {
        if (error)
            *error = QStringLiteral("Canceled");
        return false;
    }
    return ok;
#else
    Q_UNUSED(sources) Q_UNUSED(path) Q_UNUSED(format) Q_UNUSED(hooks)
    if (error)
        *error = QStringLiteral("This build of Linopus has no archive support.");
    return false;
#endif
}

bool unpack(const QString &archivePath, const QString &destDir, const QMap<QString, QString> &rename, bool replace,
            const Hooks &hooks, QStringList *created, QString *error)
{
#ifdef LINOPUS_HAVE_LIBARCHIVE
    // Encrypted: try the password on the first locked file before writing
    // anything, so a wrong one leaves nothing half-extracted behind.
    {
        struct archive *probe = openReader(archivePath, hooks.password, 64 * 1024, error);
        if (!probe)
            return false;
        struct archive_entry *pe = nullptr;
        int pr;
        int checked = 0;
        while (checked++ < 500 && ((pr = archive_read_next_header(probe, &pe)) == ARCHIVE_OK || pr == ARCHIVE_WARN)) {
            if (archive_entry_is_encrypted(pe) && archive_entry_filetype(pe) == AE_IFREG && archive_entry_size(pe) > 0) {
                const void *buf;
                size_t size;
                la_int64_t offset;
                if (archive_read_data_block(probe, &buf, &size, &offset) < ARCHIVE_WARN) {
                    if (error)
                        *error = errorOf(probe);
                    archive_read_free(probe);
                    return false;
                }
                break;
            }
            archive_read_data_skip(probe);
        }
        archive_read_free(probe);
    }
    struct archive *a = openReader(archivePath, hooks.password, 256 * 1024, error);
    if (!a)
        return false;
    struct archive *out = archive_write_disk_new();
    // Entry names are checked here and turned into full paths under the
    // destination, so absolute paths are expected; libarchive still refuses
    // ".." and writing through links the archive itself made. The
    // destination is resolved first: a link on the way to it (an SD card
    // mount, /home on some systems) would otherwise count as one.
    int flags = ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_SECURE_NODOTDOT
        | ARCHIVE_EXTRACT_SECURE_SYMLINKS;
    flags |= replace ? ARCHIVE_EXTRACT_UNLINK : ARCHIVE_EXTRACT_NO_OVERWRITE;
    archive_write_disk_set_options(out, flags);
    archive_write_disk_set_standard_lookup(out);

    const QString canonical = QFileInfo(destDir).canonicalFilePath();
    const QString dest = canonical.isEmpty() ? QDir::cleanPath(destDir) : canonical;
    const QString shownDest = QDir::cleanPath(destDir);
    QSet<QString> made;
    bool ok = true;
    bool stopped = false;
    la_int64_t lastPos = 0;
    struct archive_entry *e = nullptr;
    int r;
    auto mapPath = [&](const QString &name) -> QString {
        const QString n = cleanEntry(name);
        if (n.isEmpty() || n.startsWith(QLatin1Char('/')) || n == QLatin1String("..") || n.startsWith(QLatin1String("../"))
            || n.contains(QLatin1String("/../")) || n.endsWith(QLatin1String("/..")))
            return {};
        const QString top = n.section(QLatin1Char('/'), 0, 0);
        const QString rest = n.section(QLatin1Char('/'), 1);
        const QString mappedTop = rename.value(top, top);
        return rest.isEmpty() ? dest + QLatin1Char('/') + mappedTop : dest + QLatin1Char('/') + mappedTop + QLatin1Char('/') + rest;
    };
    while ((r = archive_read_next_header(a, &e)) == ARCHIVE_OK || r == ARCHIVE_WARN) {
        if (hooks.keepGoing && !hooks.keepGoing()) {
            stopped = true;
            break;
        }
        const QString name = entryName(e);
        const QString target = mapPath(name);
        if (target.isEmpty()) {
            if (hooks.problem && !cleanEntry(name).isEmpty())
                hooks.problem(QStringLiteral("%1: points outside the folder; left out").arg(name));
            archive_read_data_skip(a);
            continue;
        }
        archive_entry_set_pathname_utf8(e, target.toUtf8().constData());
        if (const char *hl = archive_entry_hardlink(e)) {
            const QString linkTarget = mapPath(QString::fromUtf8(hl));
            if (linkTarget.isEmpty()) {
                archive_read_data_skip(a);
                continue;
            }
            archive_entry_set_hardlink_utf8(e, linkTarget.toUtf8().constData());
        }
        const QString top = dest + QLatin1Char('/') + target.mid(dest.size() + 1).section(QLatin1Char('/'), 0, 0);
        const bool topExisted = made.contains(top) || QFileInfo::exists(top);
        if (hooks.current)
            hooks.current(target, archive_entry_size_is_set(e) ? archive_entry_size(e) : 0);
        const int w = archive_write_header(out, e);
        if (w < ARCHIVE_WARN) {
            if (hooks.problem)
                hooks.problem(QStringLiteral("%1: %2").arg(cleanEntry(name), errorOf(out)));
            archive_read_data_skip(a);
        } else {
            if (!topExisted && created)
                *created << shownDest + top.mid(dest.size()); // as the caller named the folder
            made.insert(top);
            const void *buf;
            size_t size;
            la_int64_t offset;
            int d;
            while ((d = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK) {
                if (hooks.keepGoing && !hooks.keepGoing()) {
                    stopped = true;
                    break;
                }
                if (archive_write_data_block(out, buf, size, offset) < ARCHIVE_WARN) {
                    if (hooks.problem)
                        hooks.problem(QStringLiteral("%1: %2").arg(cleanEntry(name), errorOf(out)));
                    break;
                }
                const la_int64_t pos = archive_filter_bytes(a, -1);
                if (hooks.advanced && pos > lastPos) {
                    hooks.advanced(pos - lastPos);
                    lastPos = pos;
                }
            }
            if (d < ARCHIVE_WARN) {
                // A password, damage, or a method this build cannot decode.
                if (error)
                    *error = errorOf(a);
                ok = false;
            }
            archive_write_finish_entry(out);
            if (stopped || !ok)
                break;
        }
    }
    if (r < ARCHIVE_WARN && ok && !stopped) {
        if (error)
            *error = errorOf(a);
        ok = false;
    }
    archive_write_close(out);
    archive_write_free(out);
    archive_read_free(a);
    if (stopped) {
        if (error)
            *error = QStringLiteral("Canceled");
        return false;
    }
    return ok;
#else
    Q_UNUSED(archivePath) Q_UNUSED(destDir) Q_UNUSED(rename) Q_UNUSED(replace) Q_UNUSED(hooks) Q_UNUSED(created)
    if (error)
        *error = QStringLiteral("This build of Linopus has no archive support.");
    return false;
#endif
}

} // namespace Archive
