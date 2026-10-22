#include "core/Collections.h"

#include "core/Location.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

Collections *Collections::instance()
{
    static Collections c;
    return &c;
}

bool Collections::isCollectionLocation(const QString &loc)
{
    return loc.startsWith(QLatin1String("coll:"), Qt::CaseInsensitive);
}

QString Collections::location(const QString &name)
{
    QUrl u;
    u.setScheme(QStringLiteral("coll"));
    u.setPath(QLatin1Char('/') + name);
    return u.toString(QUrl::FullyEncoded);
}

QString Collections::nameOf(const QString &loc)
{
    if (!isCollectionLocation(loc))
        return {};
    QString path = QUrl(loc).path();
    while (path.startsWith(QLatin1Char('/')))
        path.remove(0, 1);
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    return path;
}

QString Collections::dir() const
{
    return Paths::configFile(QStringLiteral("collections"));
}

QString Collections::fileFor(const QString &name) const
{
    return QDir(dir()).filePath(name + QStringLiteral(".txt"));
}

bool Collections::validName(const QString &name, QString *error)
{
    if (name.trimmed().isEmpty() || name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.'))) {
        if (error)
            *error = QStringLiteral("A collection name cannot be empty, contain / or start with a dot.");
        return false;
    }
    return true;
}

QStringList Collections::names() const
{
    QStringList out;
    for (const QFileInfo &fi : QDir(dir()).entryInfoList({QStringLiteral("*.txt")}, QDir::Files, QDir::Name | QDir::IgnoreCase))
        out << fi.completeBaseName();
    return out;
}

bool Collections::exists(const QString &name) const
{
    return !name.isEmpty() && QFileInfo::exists(fileFor(name));
}

bool Collections::create(const QString &name, QString *error)
{
    if (!validName(name, error))
        return false;
    if (exists(name)) {
        if (error)
            *error = QStringLiteral("There is already a collection called %1.").arg(name);
        return false;
    }
    setItems(name, {});
    return true;
}

bool Collections::remove(const QString &name)
{
    if (!exists(name))
        return false;
    QFile::remove(fileFor(name));
    Q_EMIT changed(QString());
    return true;
}

bool Collections::rename(const QString &from, const QString &to, QString *error)
{
    if (!validName(to, error))
        return false;
    if (!exists(from)) {
        if (error)
            *error = QStringLiteral("There is no collection called %1.").arg(from);
        return false;
    }
    if (exists(to)) {
        if (error)
            *error = QStringLiteral("There is already a collection called %1.").arg(to);
        return false;
    }
    if (!QFile::rename(fileFor(from), fileFor(to))) {
        if (error)
            *error = QStringLiteral("Could not rename the collection.");
        return false;
    }
    Q_EMIT changed(QString());
    return true;
}

QStringList Collections::items(const QString &name) const
{
    QFile f(fileFor(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QStringList out;
    for (const QByteArray &line : f.readAll().split('\n')) {
        const QString p = QString::fromUtf8(line).trimmed();
        if (!p.isEmpty() && !p.startsWith(QLatin1Char('#')))
            out << p;
    }
    return out;
}

void Collections::setItems(const QString &name, const QStringList &paths)
{
    if (!validName(name, nullptr))
        return;
    const bool isNew = !exists(name);
    QDir().mkpath(dir());
    QSaveFile f(fileFor(name));
    if (!f.open(QIODevice::WriteOnly))
        return;
    QStringList unique;
    for (const QString &p : paths) {
        const QString c = Location::clean(p);
        if (!c.isEmpty() && !unique.contains(c))
            unique << c;
    }
    f.write(unique.join(QLatin1Char('\n')).toUtf8());
    if (!unique.isEmpty())
        f.write("\n");
    f.commit();
    if (isNew)
        Q_EMIT changed(QString());
    Q_EMIT changed(name);
}

int Collections::add(const QString &name, const QStringList &paths)
{
    QStringList all = items(name);
    int added = 0;
    for (const QString &p : paths) {
        const QString c = Location::clean(p);
        if (!c.isEmpty() && !isCollectionLocation(c) && !all.contains(c)) {
            all << c;
            ++added;
        }
    }
    setItems(name, all);
    return added;
}

int Collections::removeItems(const QString &name, const QStringList &paths)
{
    QStringList all = items(name);
    int removed = 0;
    for (const QString &p : paths)
        removed += int(all.removeAll(Location::clean(p)));
    if (removed)
        setItems(name, all);
    return removed;
}

void Collections::renamed(const QString &rawFrom, const QString &rawTo)
{
    const QString from = Location::clean(rawFrom);
    const QString to = Location::clean(rawTo);
    for (const QString &name : names()) {
        QStringList all = items(name);
        bool touched = false;
        for (QString &p : all) {
            if (p == from) {
                p = to;
                touched = true;
            } else if (p.startsWith(from + QLatin1Char('/'))) {
                p = to + p.mid(from.size()); // inside a renamed folder
                touched = true;
            }
        }
        if (touched)
            setItems(name, all);
    }
}
