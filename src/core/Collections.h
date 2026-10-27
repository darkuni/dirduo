#pragma once

#include <QObject>
#include <QStringList>

// Collections: named lists of links to real files and folders, anywhere on
// any drive. A collection shows as a folder at coll:/<name>; what is in it
// are the real items, so opening, copying from and renaming them act on the
// real files, while Delete only takes them out of the collection.
// Each one is a plain text file in ~/.config/dirduo/collections, one path
// per line.
class Collections : public QObject
{
    Q_OBJECT
public:
    static Collections *instance();

    static constexpr const char *kRoot = "coll:/";
    static bool isCollectionLocation(const QString &loc);
    static QString location(const QString &name); // coll:/<name>
    static QString nameOf(const QString &loc);     // empty for the root

    QStringList names() const;
    bool exists(const QString &name) const;
    bool create(const QString &name, QString *error = nullptr);
    bool remove(const QString &name);
    bool rename(const QString &from, const QString &to, QString *error = nullptr);

    QStringList items(const QString &name) const;
    void setItems(const QString &name, const QStringList &paths); // creates it when needed
    int add(const QString &name, const QStringList &paths);       // returns how many were new
    int removeItems(const QString &name, const QStringList &paths);
    // A real item was renamed or moved: every collection follows it.
    void renamed(const QString &from, const QString &to);

    QString dir() const;

Q_SIGNALS:
    void changed(const QString &name); // empty: the list of collections

private:
    Collections() = default;
    QString fileFor(const QString &name) const;

public:
    static bool validName(const QString &name, QString *error);
};
