#include "core/TabGroups.h"

#include "core/Paths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

TabGroups *TabGroups::instance()
{
    static TabGroups *t = new TabGroups;
    return t;
}

TabGroups::TabGroups()
{
    reload();
}

QString TabGroups::file() const
{
    return Paths::configFile(QStringLiteral("tabgroups.json"));
}

void TabGroups::reload()
{
    m_groups.clear();
    QFile f(file());
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QJsonArray a = QJsonDocument::fromJson(f.readAll()).object().value(QStringLiteral("groups")).toArray();
    for (const QJsonValue &v : a) {
        const QJsonObject o = v.toObject();
        Group g;
        g.name = o.value(QStringLiteral("name")).toString().trimmed();
        for (const QJsonValue &p : o.value(QStringLiteral("left")).toArray())
            g.left << p.toString();
        for (const QJsonValue &p : o.value(QStringLiteral("right")).toArray())
            g.right << p.toString();
        g.leftCurrent = o.value(QStringLiteral("leftCurrent")).toInt();
        g.rightCurrent = o.value(QStringLiteral("rightCurrent")).toInt();
        g.closeOthers = o.value(QStringLiteral("closeOthers")).toBool(true);
        if (!g.name.isEmpty() && !g.left.isEmpty())
            m_groups << g;
    }
}

void TabGroups::write()
{
    QJsonArray a;
    for (const Group &g : std::as_const(m_groups)) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), g.name);
        o.insert(QStringLiteral("left"), QJsonArray::fromStringList(g.left));
        if (!g.right.isEmpty())
            o.insert(QStringLiteral("right"), QJsonArray::fromStringList(g.right));
        o.insert(QStringLiteral("leftCurrent"), g.leftCurrent);
        o.insert(QStringLiteral("rightCurrent"), g.rightCurrent);
        o.insert(QStringLiteral("closeOthers"), g.closeOthers);
        a.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("groups"), a);
    QDir().mkpath(QFileInfo(file()).absolutePath());
    QSaveFile f(file());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(root).toJson());
        f.commit();
    }
}

QStringList TabGroups::names() const
{
    QStringList out;
    for (const Group &g : m_groups)
        out << g.name;
    return out;
}

bool TabGroups::has(const QString &name) const
{
    return !group(name).name.isEmpty();
}

TabGroups::Group TabGroups::group(const QString &name) const
{
    for (const Group &g : m_groups)
        if (g.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return g;
    return {};
}

void TabGroups::save(const Group &g)
{
    for (Group &x : m_groups) {
        if (x.name.compare(g.name, Qt::CaseInsensitive) == 0) {
            x = g;
            write();
            Q_EMIT changed();
            return;
        }
    }
    m_groups << g;
    write();
    Q_EMIT changed();
}

bool TabGroups::remove(const QString &name)
{
    for (int i = 0; i < m_groups.size(); ++i) {
        if (m_groups.at(i).name.compare(name, Qt::CaseInsensitive) == 0) {
            m_groups.removeAt(i);
            write();
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

bool TabGroups::rename(const QString &from, const QString &to)
{
    if (to.trimmed().isEmpty() || (has(to) && from.compare(to, Qt::CaseInsensitive) != 0))
        return false;
    for (Group &g : m_groups) {
        if (g.name.compare(from, Qt::CaseInsensitive) == 0) {
            g.name = to.trimmed();
            write();
            Q_EMIT changed();
            return true;
        }
    }
    return false;
}

void TabGroups::setGroups(const QList<Group> &groups)
{
    m_groups = groups;
    write();
    Q_EMIT changed();
}
