#include "core/Checksum.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>

#include <array>

namespace {

const std::array<quint32, 256> &crcTable()
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[i] = c;
        }
        return t;
    }();
    return table;
}

quint32 crcUpdate(quint32 crc, const char *data, qint64 len)
{
    const auto &t = crcTable();
    crc = ~crc;
    for (qint64 i = 0; i < len; ++i)
        crc = t[(crc ^ quint8(data[i])) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

QString crcHex(quint32 crc)
{
    return QStringLiteral("%1").arg(crc, 8, 16, QLatin1Char('0')).toUpper();
}

QCryptographicHash::Algorithm qtAlgo(Checksum::Algo a)
{
    switch (a) {
    case Checksum::Algo::Md5: return QCryptographicHash::Md5;
    case Checksum::Algo::Sha1: return QCryptographicHash::Sha1;
    default: return QCryptographicHash::Sha256;
    }
}

ChecksumCache *g_checksums = nullptr;

} // namespace

namespace Checksum {

QList<Algo> all()
{
    return {Algo::Crc32, Algo::Md5, Algo::Sha1, Algo::Sha256};
}

bool fromName(const QString &n, Algo *out)
{
    const QString s = n.trimmed().toLower().remove(QLatin1Char('-'));
    Algo a;
    if (s == QLatin1String("md5"))
        a = Algo::Md5;
    else if (s == QLatin1String("sha1"))
        a = Algo::Sha1;
    else if (s == QLatin1String("sha256"))
        a = Algo::Sha256;
    else if (s == QLatin1String("crc32") || s == QLatin1String("crc") || s == QLatin1String("sfv"))
        a = Algo::Crc32;
    else
        return false;
    if (out)
        *out = a;
    return true;
}

QString name(Algo a)
{
    switch (a) {
    case Algo::Crc32: return QStringLiteral("crc32");
    case Algo::Md5: return QStringLiteral("md5");
    case Algo::Sha1: return QStringLiteral("sha1");
    case Algo::Sha256: return QStringLiteral("sha256");
    }
    return {};
}

QString label(Algo a)
{
    switch (a) {
    case Algo::Crc32: return QStringLiteral("CRC32");
    case Algo::Md5: return QStringLiteral("MD5");
    case Algo::Sha1: return QStringLiteral("SHA-1");
    case Algo::Sha256: return QStringLiteral("SHA-256");
    }
    return {};
}

QString extension(Algo a)
{
    return a == Algo::Crc32 ? QStringLiteral("sfv") : name(a);
}

int hexLength(Algo a)
{
    switch (a) {
    case Algo::Crc32: return 8;
    case Algo::Md5: return 32;
    case Algo::Sha1: return 40;
    case Algo::Sha256: return 64;
    }
    return 0;
}

QString compute(const QString &path, Algo a, const std::function<bool(qint64)> &progress, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    QCryptographicHash hash(qtAlgo(a));
    quint32 crc = 0;
    QByteArray buf(1 << 20, Qt::Uninitialized);
    qint64 done = 0;
    for (;;) {
        const qint64 n = f.read(buf.data(), buf.size());
        if (n < 0) {
            if (error)
                *error = f.errorString();
            return {};
        }
        if (n == 0)
            break;
        if (a == Algo::Crc32)
            crc = crcUpdate(crc, buf.constData(), n);
        else
            hash.addData(QByteArrayView(buf.constData(), n));
        done += n;
        if (progress && !progress(done)) {
            if (error)
                error->clear();
            return {};
        }
    }
    return a == Algo::Crc32 ? crcHex(crc) : QString::fromLatin1(hash.result().toHex());
}

QString computeData(const QByteArray &data, Algo a)
{
    if (a == Algo::Crc32)
        return crcHex(crcUpdate(0, data.constData(), data.size()));
    return QString::fromLatin1(QCryptographicHash::hash(data, qtAlgo(a)).toHex());
}

bool isListFile(const QString &fileName)
{
    const QString ext = QFileInfo(fileName).suffix().toLower();
    static const QStringList exts = {QStringLiteral("sfv"),       QStringLiteral("md5"),       QStringLiteral("sha1"),
                                     QStringLiteral("sha256"),    QStringLiteral("md5sum"),    QStringLiteral("sha1sum"),
                                     QStringLiteral("sha256sum"), QStringLiteral("md5sums"),   QStringLiteral("sha256sums")};
    return exts.contains(ext) || fileName.compare(QLatin1String("MD5SUMS"), Qt::CaseInsensitive) == 0
        || fileName.compare(QLatin1String("SHA256SUMS"), Qt::CaseInsensitive) == 0
        || fileName.compare(QLatin1String("SHA1SUMS"), Qt::CaseInsensitive) == 0;
}

QList<Entry> parse(const QString &text, const QString &fileName)
{
    QList<Entry> out;
    const QString base = QFileInfo(fileName).fileName().toLower();
    const QString ext = QFileInfo(fileName).suffix().toLower();
    const bool sfv = ext == QLatin1String("sfv");
    Algo byName = Algo::Md5;
    bool named = false;
    if (base.startsWith(QLatin1String("sha256")) || ext.startsWith(QLatin1String("sha256"))) {
        byName = Algo::Sha256;
        named = true;
    } else if (base.startsWith(QLatin1String("sha1")) || ext.startsWith(QLatin1String("sha1"))) {
        byName = Algo::Sha1;
        named = true;
    } else if (base.startsWith(QLatin1String("md5")) || ext.startsWith(QLatin1String("md5"))) {
        byName = Algo::Md5;
        named = true;
    }
    static const QRegularExpression bsd(QStringLiteral("^(MD5|SHA1|SHA256|SHA-1|SHA-256|CRC32)\\s*\\((.*)\\)\\s*=\\s*([0-9A-Fa-f]+)$"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression gnu(QStringLiteral("^\\\\?([0-9A-Fa-f]{8,128})\\s[ *]?(.+)$"));
    static const QRegularExpression sfvLine(QStringLiteral("^(.+?)\\s+([0-9A-Fa-f]{8})$"));
    for (QString line : text.split(QLatin1Char('\n'))) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('#')))
            continue;
        Entry e;
        if (sfv) {
            const auto m = sfvLine.match(line);
            if (!m.hasMatch())
                continue;
            e.name = m.captured(1);
            e.hash = m.captured(2).toUpper();
            e.algo = Algo::Crc32;
        } else if (const auto m = bsd.match(line); m.hasMatch()) {
            if (!fromName(m.captured(1), &e.algo))
                continue;
            e.name = m.captured(2);
            e.hash = m.captured(3);
        } else if (const auto g = gnu.match(line); g.hasMatch()) {
            e.hash = g.captured(1);
            e.name = g.captured(2);
            if (named && e.hash.size() == hexLength(byName)) {
                e.algo = byName;
            } else {
                bool found = false;
                for (Algo a : {Algo::Md5, Algo::Sha1, Algo::Sha256, Algo::Crc32}) {
                    if (e.hash.size() == hexLength(a)) {
                        e.algo = a;
                        found = true;
                        break;
                    }
                }
                if (!found)
                    continue;
            }
        } else {
            continue;
        }
        if (e.hash.size() != hexLength(e.algo))
            continue;
        e.hash = e.algo == Algo::Crc32 ? e.hash.toUpper() : e.hash.toLower();
        e.name.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (e.name.startsWith(QLatin1String("./")))
            e.name.remove(0, 2);
        out << e;
    }
    return out;
}

QList<Entry> readList(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    if (f.size() > 64 * 1024 * 1024) {
        if (error)
            *error = QStringLiteral("It is too big to be a checksum list.");
        return {};
    }
    return parse(QString::fromUtf8(f.readAll()), QFileInfo(path).fileName());
}

QString format(const QList<Entry> &entries, Algo a)
{
    QString out;
    if (a == Algo::Crc32) {
        out += QStringLiteral("; Made by DirDuo\n");
        for (const Entry &e : entries)
            out += e.name + QLatin1Char(' ') + e.hash.toUpper() + QLatin1Char('\n');
        return out;
    }
    for (const Entry &e : entries)
        out += e.hash.toLower() + QStringLiteral("  ") + e.name + QLatin1Char('\n');
    return out;
}

QString resultText(Result r)
{
    switch (r) {
    case Result::Ok: return QStringLiteral("OK");
    case Result::Bad: return QStringLiteral("Does not match");
    case Result::Missing: return QStringLiteral("Missing");
    case Result::Unreadable: return QStringLiteral("Cannot be read");
    }
    return {};
}

} // namespace Checksum

ChecksumCache::ChecksumCache(QObject *parent)
    : QObject(parent)
    , m_cancel(std::make_shared<std::atomic<bool>>(false))
{
    m_pool.setMaxThreadCount(1); // whole files are read; one at a time is kinder to an SD card
}

ChecksumCache::~ChecksumCache()
{
    *m_cancel = true;
    m_pool.clear();
    m_pool.waitForDone();
    if (g_checksums == this)
        g_checksums = nullptr;
}

ChecksumCache *ChecksumCache::instance()
{
    if (!g_checksums)
        g_checksums = new ChecksumCache(qApp);
    return g_checksums;
}

QString ChecksumCache::key(const QString &path, Checksum::Algo a)
{
    return Checksum::name(a) + QLatin1Char(':') + path;
}

bool ChecksumCache::lookup(const QString &path, Checksum::Algo a, qint64 size, const QDateTime &modified, QString *out)
{
    const QString k = key(path, a);
    {
        QMutexLocker lock(&m_mutex);
        const auto it = m_cache.constFind(k);
        if (it != m_cache.constEnd() && it->size == size && it->modified == modified) {
            if (out)
                *out = it->hash;
            return true;
        }
        if (m_pending.contains(k))
            return false;
        m_pending.insert(k);
    }
    QPointer<ChecksumCache> self(this);
    auto cancel = m_cancel;
    m_pool.start([self, path, a, k, size, modified, cancel]() {
        if (*cancel)
            return;
        const QString hash = Checksum::compute(path, a, [cancel](qint64) { return !*cancel; });
        if (*cancel)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [self, path, k, hash, size, modified]() {
                if (!self)
                    return;
                {
                    QMutexLocker lock(&self->m_mutex);
                    if (!self->m_pending.remove(k))
                        return;
                    self->m_cache.insert(k, Entry{hash, size, modified});
                }
                Q_EMIT self->ready(path);
            },
            Qt::QueuedConnection);
    });
    return false;
}

void ChecksumCache::clear()
{
    QMutexLocker lock(&m_mutex);
    m_cache.clear();
    m_pending.clear();
}
