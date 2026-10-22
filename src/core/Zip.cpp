#include "core/Zip.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>
#include <QtEndian>

#ifdef LINOPUS_HAVE_ZLIB
#include <zlib.h>
#endif

namespace {

void put16(QByteArray &b, quint16 v)
{
    char c[2];
    qToLittleEndian(v, c);
    b.append(c, 2);
}

void put32(QByteArray &b, quint32 v)
{
    char c[4];
    qToLittleEndian(v, c);
    b.append(c, 4);
}

quint16 get16(const QByteArray &b, qsizetype at)
{
    return qFromLittleEndian<quint16>(b.constData() + at);
}

quint32 get32(const QByteArray &b, qsizetype at)
{
    return qFromLittleEndian<quint32>(b.constData() + at);
}

void dosTime(const QDateTime &dt, quint16 &time, quint16 &date)
{
    const QDate d = dt.date();
    const QTime t = dt.time();
    time = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    date = quint16(((qMax(1980, d.year()) - 1980) << 9) | (d.month() << 5) | d.day());
}

#ifdef LINOPUS_HAVE_ZLIB
QByteArray inflateRaw(const QByteArray &raw, quint32 size, bool *ok)
{
    QByteArray out(qsizetype(size), Qt::Uninitialized);
    z_stream zs{};
    *ok = false;
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
        return {};
    zs.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(raw.constData()));
    zs.avail_in = uInt(raw.size());
    zs.next_out = reinterpret_cast<Bytef *>(out.data());
    zs.avail_out = uInt(out.size());
    const int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (rc != Z_STREAM_END || zs.total_out != size)
        return {};
    *ok = true;
    return out;
}
#endif

} // namespace

namespace Zip {

quint32 crc32(const QByteArray &data)
{
    static quint32 table[256];
    static bool ready = false;
    if (!ready) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (const char ch : data)
        crc = table[(crc ^ quint8(ch)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool write(const QString &file, const QList<Entry> &entries, QString *error)
{
    QByteArray out;
    QByteArray central;
    quint16 time = 0;
    quint16 date = 0;
    dosTime(QDateTime::currentDateTime(), time, date);
    for (const Entry &e : entries) {
        const QByteArray name = e.name.toUtf8();
        const quint32 crc = crc32(e.data);
        const quint32 offset = quint32(out.size());
        put32(out, 0x04034b50);
        put16(out, 20);     // version needed
        put16(out, 0x0800); // UTF-8 names
        put16(out, 0);      // stored
        put16(out, time);
        put16(out, date);
        put32(out, crc);
        put32(out, quint32(e.data.size()));
        put32(out, quint32(e.data.size()));
        put16(out, quint16(name.size()));
        put16(out, 0);
        out += name;
        out += e.data;

        put32(central, 0x02014b50);
        put16(central, 0x031e); // made by Unix, 3.0
        put16(central, 20);
        put16(central, 0x0800);
        put16(central, 0);
        put16(central, time);
        put16(central, date);
        put32(central, crc);
        put32(central, quint32(e.data.size()));
        put32(central, quint32(e.data.size()));
        put16(central, quint16(name.size()));
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put16(central, 0);
        put32(central, quint32(0100644) << 16); // -rw-r--r--
        put32(central, offset);
        central += name;
    }
    const quint32 centralOffset = quint32(out.size());
    out += central;
    put32(out, 0x06054b50);
    put16(out, 0);
    put16(out, 0);
    put16(out, quint16(entries.size()));
    put16(out, quint16(entries.size()));
    put32(out, quint32(central.size()));
    put32(out, centralOffset);
    put16(out, 0);

    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly) || f.write(out) != out.size() || !f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool read(const QString &file, QList<Entry> &entries, QString *error)
{
    auto fail = [error](const QString &msg) {
        if (error)
            *error = msg;
        return false;
    };
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return fail(f.errorString());
    const QByteArray b = f.readAll();
    // End of central directory: the last signature in the file.
    qsizetype eocd = -1;
    for (qsizetype i = b.size() - 22; i >= 0 && i >= b.size() - 22 - 65535; --i) {
        if (get32(b, i) == 0x06054b50) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0)
        return fail(QStringLiteral("This is not a zip file."));
    const int count = get16(b, eocd + 10);
    qsizetype at = get32(b, eocd + 16);
    entries.clear();
    for (int n = 0; n < count; ++n) {
        if (at + 46 > b.size() || get32(b, at) != 0x02014b50)
            return fail(QStringLiteral("The zip file is damaged."));
        const quint16 method = get16(b, at + 10);
        const quint32 crc = get32(b, at + 16);
        const quint32 csize = get32(b, at + 20);
        const quint32 usize = get32(b, at + 24);
        const quint16 nlen = get16(b, at + 28);
        const quint16 xlen = get16(b, at + 30);
        const quint16 clen = get16(b, at + 32);
        const quint32 local = get32(b, at + 42);
        const QString name = QString::fromUtf8(b.mid(at + 46, nlen));
        at += 46 + nlen + xlen + clen;
        if (local + 30 > quint32(b.size()) || get32(b, local) != 0x04034b50)
            return fail(QStringLiteral("The zip file is damaged."));
        const qsizetype dataAt = local + 30 + get16(b, local + 26) + get16(b, local + 28);
        const QByteArray raw = b.mid(dataAt, csize);
        QByteArray data;
        if (method == 0) {
            data = raw;
        } else if (method == 8) {
#ifdef LINOPUS_HAVE_ZLIB
            bool ok = false;
            data = inflateRaw(raw, usize, &ok);
            if (!ok)
                return fail(QStringLiteral("%1 is damaged.").arg(name));
#else
            return fail(QStringLiteral("%1 is compressed; this build reads only uncompressed zips.").arg(name));
#endif
        } else {
            return fail(QStringLiteral("%1 uses a compression method Linopus cannot read.").arg(name));
        }
        if (crc32(data) != crc)
            return fail(QStringLiteral("%1 is damaged (checksum mismatch).").arg(name));
        if (!name.endsWith(QLatin1Char('/')))
            entries << Entry{name, data};
    }
    return true;
}

} // namespace Zip
