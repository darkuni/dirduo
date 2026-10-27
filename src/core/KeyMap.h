#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>

// Keyboard shortcuts, as Opus's Keys page has them: the keys of the menu
// commands (changed from their defaults, or taken off), and hotkeys of your
// own that run any commands. Saved in ~/.config/dirduo/keys.json. Keys are
// QKeySequence portable text ("Ctrl+Shift+K").
class KeyMap : public QObject
{
    Q_OBJECT
public:
    // A menu command the listers have; registered as they build their menus.
    struct MenuItem
    {
        QString id;       // "Edit > Copy"
        QString command;  // what it runs, when it is a DirDuo command
        QStringList defaults;
    };
    struct Hotkey
    {
        QString name;
        QString key;
        QString command; // one or more lines of DirDuo commands
        bool enabled = true;
        bool operator==(const Hotkey &o) const
        {
            return name == o.name && key == o.key && command == o.command && enabled == o.enabled;
        }
    };

    static KeyMap *instance();
    QString file() const;
    void setFile(const QString &file); // tests: reads it
    void reload();

    void registerMenuItem(const MenuItem &item);
    QList<MenuItem> menuItems() const { return m_items; }
    MenuItem menuItem(const QString &id) const;
    // The keys a menu command has now: changed ones, else its defaults.
    QStringList keysFor(const QString &id) const;
    bool isChanged(const QString &id) const { return m_overrides.contains(id); }
    QHash<QString, QStringList> overrides() const { return m_overrides; }

    QList<Hotkey> hotkeys() const { return m_hotkeys; }

    // Replaces everything, saves and tells the listers.
    void set(const QHash<QString, QStringList> &overrides, const QList<Hotkey> &hotkeys);

    // Hotkeys to and from a file of their own (to share them, as Opus's
    // "Importable Hotkey Format").
    static bool exportHotkeys(const QString &file, const QList<Hotkey> &hotkeys, QString *error);
    static QList<Hotkey> importHotkeys(const QString &file, bool *ok, QString *error);

    // A key as the menus show it ("Ctrl+Shift+K"), from portable text.
    static QString display(const QString &portable);
    static QString portable(const class QKeySequence &seq);

Q_SIGNALS:
    void changed();

private:
    KeyMap();
    bool save(QString *error = nullptr);

    QString m_file; // tests; else keys.json in the settings folder
    QList<MenuItem> m_items;
    QHash<QString, QStringList> m_overrides;
    QList<Hotkey> m_hotkeys;
};
