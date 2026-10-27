#pragma once

#include <QList>
#include <QObject>
#include <QStringList>

// Tab groups, as Opus's: the tabs of both displays saved under a name, to open
// again all at once (Go TABGROUPLOAD=name). Saved in
// ~/.config/dirduo/tabgroups.json.
class TabGroups : public QObject
{
    Q_OBJECT
public:
    struct Group
    {
        QString name;
        QStringList left;   // the first (left or top) display's tabs
        QStringList right;  // the second's; empty for one display
        int leftCurrent = 0;
        int rightCurrent = 0;
        bool closeOthers = true; // close the tabs that are open first (else add to them)
    };

    static TabGroups *instance();
    QString file() const;
    void reload();

    QList<Group> groups() const { return m_groups; }
    QStringList names() const;
    bool has(const QString &name) const;
    Group group(const QString &name) const;
    void save(const Group &g); // adds, or replaces the one with its name
    bool remove(const QString &name);
    bool rename(const QString &from, const QString &to);
    void setGroups(const QList<Group> &groups);

Q_SIGNALS:
    void changed();

private:
    TabGroups();
    void write();
    QList<Group> m_groups;
};
