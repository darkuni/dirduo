#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

// Favourites: folders you picked, in your order, optionally in groups (one
// menu level each). Kept in favorites.json.
struct Favorite
{
    QString name;
    QString path;            // empty for a group or a separator
    QList<Favorite> items;   // a group's entries
    bool separator = false;
    bool isGroup() const { return path.isEmpty() && !separator; }
    bool operator==(const Favorite &o) const
    {
        return name == o.name && path == o.path && items == o.items && separator == o.separator;
    }
};

// Frequent folders ("smart favourites"): every folder read is counted; the
// list ranks them by visits, with older visits counting for less.
struct FrequentFolder
{
    QString path;
    int visits = 0;
    QDateTime last;
    double score = 0;
};

class Favorites : public QObject
{
    Q_OBJECT
public:
    static Favorites *instance();

    QList<Favorite> items() const;
    void setItems(const QList<Favorite> &items);
    bool contains(const QString &path) const;
    // Adds at the end (or to a group); the name defaults to the folder's.
    void add(const QString &path, const QString &name = {}, const QString &group = {});
    bool removePath(const QString &path);
    QString pathFor(const QString &name) const; // by name, groups included

    void visit(const QString &path);
    QList<FrequentFolder> frequent(int max = 12) const;
    void clearFrequent();

Q_SIGNALS:
    void changed();

private:
    Favorites() = default;
    QString file() const;
    QString frequentFile() const;
};
