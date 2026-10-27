#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

// Named locations, written "/name" anywhere a path is accepted (address bar,
// Go command, Copy TO=, button code). "/downloads/podcasts" resolves to the
// Downloads folder plus "podcasts".
//
// On Linux a leading slash is also the filesystem root, so alias names may not
// shadow a real top-level directory: "/usr" always means /usr. Built-in names
// avoid the standard root directories, and user aliases that collide with
// something in "/" are refused when they are defined.
//
// User aliases live in ~/.config/dirduo/aliases.ini and override built-ins.
class AliasManager
{
public:
    // configFile empty means ~/.config/dirduo/aliases.ini.
    explicit AliasManager(const QString &configFile = {});

    void load();
    bool save() const;

    // "/name/rest" -> alias target + rest. "~" and "~/rest" -> home.
    // "file:///x" -> /x. Anything else is returned cleaned and unchanged.
    // Relative input stays relative; callers resolve it against a folder.
    QString resolve(const QString &input) const;

    bool hasAlias(const QString &name) const;
    QString aliasPath(const QString &name) const; // empty if unknown or unavailable
    bool isBuiltIn(const QString &name) const;
    bool isUserAlias(const QString &name) const;

    // Sorted names (lower case) of every alias that currently resolves.
    QStringList names() const;

    // "/dow" -> {"/downloads"}. Prefix must start with "/".
    QStringList completions(const QString &prefix) const;

    bool setUserAlias(const QString &name, const QString &path, QString *error = nullptr);
    bool removeUserAlias(const QString &name);

    // True for names that would shadow a real directory in "/".
    static bool isReservedName(const QString &name);
    static bool isValidName(const QString &name);

private:
    void buildBuiltIns();
    QString dynamicAlias(const QString &key) const;

    QString m_file;
    QMap<QString, QString> m_builtIns; // static built-ins, key lower case
    QMap<QString, QString> m_user;     // key lower case
};
