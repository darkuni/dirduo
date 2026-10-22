#pragma once

#include <QByteArray>
#include <QList>
#include <QString>

// Just enough zip for settings backups: writes stored (uncompressed) entries,
// which every archiver opens, and reads stored or deflated ones, so a backup
// re-zipped by another tool still restores.
namespace Zip {

struct Entry
{
    QString name; // forward slashes, no leading slash
    QByteArray data;
};

bool write(const QString &file, const QList<Entry> &entries, QString *error = nullptr);
bool read(const QString &file, QList<Entry> &entries, QString *error = nullptr);
quint32 crc32(const QByteArray &data);

} // namespace Zip
