#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QStringList>

#include <functional>

// The right-click menu, made your own (as Opus's File Types): commands of
// your own for every file, for files or folders, for one kind (pictures,
// disk images...), for names matching wildcards (*.d64;*.prg) or for empty
// space in a list; and the built-in items you would rather not see. Saved in
// ~/.config/dirduo/contextmenu.json.
class ContextMenus : public QObject
{
    Q_OBJECT
public:
    struct Item
    {
        QString label;
        QString icon;
        QString command;  // one or more lines of DirDuo commands
        QString applies = QStringLiteral("files"); // all, files, folders, background, a kind key (image...), or wildcards
        QString submenu;  // put it in a submenu with this name
        bool several = true; // also when several items are selected
        bool enabled = true;
        bool operator==(const Item &o) const
        {
            return label == o.label && icon == o.icon && command == o.command && applies == o.applies && submenu == o.submenu
                && several == o.several && enabled == o.enabled;
        }
    };

    static ContextMenus *instance();
    QString file() const;
    void reload();

    QList<Item> items() const { return m_items; }
    QStringList hidden() const { return m_hidden; }
    bool isHidden(const QString &label) const { return m_hidden.contains(canonical(label)); }
    void set(const QList<Item> &items, const QStringList &hidden);

    // The built-in items, as the Right-Click Menu page lists them.
    static QStringList builtInLabels();
    // The name a built-in item goes by: "Convert Images..." -> "Convert Image".
    static QString canonical(const QString &text);
    // What "applies" can be, with how it reads: ("image", "Pictures")...
    static QList<QPair<QString, QString>> appliesChoices();
    static QString appliesText(const QString &applies);

    // Whether the item belongs in the menu for these items (empty: empty space).
    static bool matches(const Item &item, const QStringList &paths, const std::function<bool(const QString &)> &isFolder);

Q_SIGNALS:
    void changed();

private:
    ContextMenus();
    void write();
    QList<Item> m_items;
    QStringList m_hidden;
};
