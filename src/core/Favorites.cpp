#include "core/Favorites.h"

#include "core/Location.h"
#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>

#include <algorithm>
#include <cmath>

namespace {

QJsonArray toJson(const QList<Favorite> &list)
{
    QJsonArray arr;
    for (const Favorite &f : list) {
        QJsonObject o;
        if (f.separator) {
            o.insert(QStringLiteral("separator"), true);
        } else {
            o.insert(QStringLiteral("name"), f.name);
            if (f.isGroup())
                o.insert(QStringLiteral("items"), toJson(f.items));
            else
                o.insert(QStringLiteral("path"), f.path);
        }
        arr.append(o);
    }
    return arr;
}

QList<Favorite> fromJson(const QJsonArray &arr)
{
    QList<Favorite> out;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        Favorite f;
        f.separator = o.value(QStringLiteral("separator")).toBool();
        f.name = o.value(QStringLiteral("name")).toString();
        f.path = o.value(QStringLiteral("path")).toString();
        if (o.contains(QStringLiteral("items")))
            f.items = fromJson(o.value(QStringLiteral("items")).toArray());
        if (f.separator || !f.name.isEmpty())
            out << f;
    }
    return out;
}

bool removeIn(QList<Favorite> &list, const QString &path)
{
    for (int i = 0; i < list.size(); ++i) {
        if (!list[i].isGroup() && list[i].path == path) {
            list.removeAt(i);
            return true;
        }
        if (list[i].isGroup() && removeIn(list[i].items, path))
            return true;
    }
    return false;
}

bool containsIn(const QList<Favorite> &list, const QString &path)
{
    for (const Favorite &f : list) {
        if ((!f.isGroup() && f.path == path) || (f.isGroup() && containsIn(f.items, path)))
            return true;
    }
    return false;
}

QString findName(const QList<Favorite> &list, const QString &name)
{
    for (const Favorite &f : list) {
        if (!f.isGroup() && !f.separator && f.name.compare(name, Qt::CaseInsensitive) == 0)
            return f.path;
        if (f.isGroup()) {
            const QString p = findName(f.items, name);
            if (!p.isEmpty())
                return p;
        }
    }
    return {};
}

} // namespace

Favorites *Favorites::instance()
{
    static Favorites f;
    return &f;
}

QString Favorites::file() const
{
    return Paths::configFile(QStringLiteral("favorites.json"));
}

QString Favorites::frequentFile() const
{
    return Paths::configFile(QStringLiteral("frequent.ini"));
}

QList<Favorite> Favorites::items() const
{
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return fromJson(QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("favorites")).toArray());
}

void Favorites::setItems(const QList<Favorite> &list)
{
    QDir().mkpath(Paths::configDir());
    QJsonObject root;
    root.insert(QStringLiteral("favorites"), toJson(list));
    QSaveFile f(file());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.commit();
    }
    Q_EMIT changed();
}

bool Favorites::contains(const QString &path) const
{
    return containsIn(items(), Location::clean(path));
}

void Favorites::add(const QString &rawPath, const QString &name, const QString &group)
{
    const QString path = Location::clean(rawPath);
    QList<Favorite> list = items();
    Favorite f;
    f.path = path;
    f.name = name.isEmpty() ? Location::title(path) : name;
    if (group.isEmpty()) {
        list << f;
    } else {
        bool placed = false;
        for (Favorite &g : list) {
            if (g.isGroup() && g.name == group) {
                g.items << f;
                placed = true;
                break;
            }
        }
        if (!placed) {
            Favorite g;
            g.name = group;
            g.items << f;
            list << g;
        }
    }
    setItems(list);
}

bool Favorites::removePath(const QString &path)
{
    QList<Favorite> list = items();
    if (!removeIn(list, Location::clean(path)))
        return false;
    setItems(list);
    return true;
}

QString Favorites::pathFor(const QString &name) const
{
    return findName(items(), name);
}

void Favorites::visit(const QString &rawPath)
{
    const QString path = Location::clean(rawPath);
    if (path.isEmpty())
        return;
    QSettings s(frequentFile(), QSettings::IniFormat);
    // Keys are hex, so any path (spaces, slashes, URLs) is safe as a key.
    const QString key = QStringLiteral("f/") + QString::fromLatin1(path.toUtf8().toHex());
    const QStringList v = s.value(key).toStringList();
    const int visits = v.value(0).toInt() + 1;
    s.setValue(key, QStringList{QString::number(visits), QDateTime::currentDateTime().toString(Qt::ISODate)});
}

QList<FrequentFolder> Favorites::frequent(int max) const
{
    QSettings s(frequentFile(), QSettings::IniFormat);
    s.beginGroup(QStringLiteral("f"));
    QList<FrequentFolder> out;
    const QDateTime now = QDateTime::currentDateTime();
    for (const QString &key : s.childKeys()) {
        const QStringList v = s.value(key).toStringList();
        FrequentFolder f;
        f.path = QString::fromUtf8(QByteArray::fromHex(key.toLatin1()));
        f.visits = v.value(0).toInt();
        f.last = QDateTime::fromString(v.value(1), Qt::ISODate);
        // A visit counts half as much after a month.
        const double days = f.last.isValid() ? f.last.secsTo(now) / 86400.0 : 365.0;
        f.score = f.visits * std::pow(0.5, days / 30.0);
        if (f.visits > 0)
            out << f;
    }
    std::sort(out.begin(), out.end(), [](const FrequentFolder &a, const FrequentFolder &b) {
        return a.score != b.score ? a.score > b.score : a.path < b.path;
    });
    if (out.size() > max)
        out = out.mid(0, max);
    return out;
}

void Favorites::clearFrequent()
{
    QFile::remove(frequentFile());
    Q_EMIT changed();
}
