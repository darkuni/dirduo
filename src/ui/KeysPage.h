#pragma once

#include "core/KeyMap.h"

#include <QHash>
#include <QWidget>

class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;
class ToolbarStore;

// Preferences > Keys, as Opus's Keys page: every key in one list. Hotkeys of
// your own (any commands), the menu commands' keys (changed, taken off, or
// back to the default) and the hotkeys of toolbar buttons. Keys used twice
// show in red; giving a key that is in use asks to take it from the other.
// Nothing changes until Apply or OK.
class KeysPage : public QWidget
{
    Q_OBJECT
public:
    KeysPage(ToolbarStore &toolbars, QWidget *parent = nullptr);

    QTreeWidget *list() const { return m_list; }
    // What a row is: "hotkey" (with index), "menu" (with id) or "button" (bar and index).
    struct Ref
    {
        QString kind;
        QString id;
        int index = -1;
        bool operator==(const Ref &o) const { return kind == o.kind && id == o.id && index == o.index; }
    };
    static Ref refOf(const QTreeWidgetItem *item);
    QTreeWidgetItem *itemFor(const Ref &ref) const;
    QTreeWidgetItem *menuItem(const QString &id) const { return itemFor({QStringLiteral("menu"), id, -1}); }

    void addHotkey(const KeyMap::Hotkey &h);
    QList<KeyMap::Hotkey> hotkeys() const { return m_hotkeys; }
    // Gives the row this key (empty: none). When another has it, takes it
    // from that one when take is true; otherwise both keep it (a conflict).
    void setKey(const Ref &ref, const QString &portableKey, bool take);
    void resetMenu(const QString &id);
    void resetAll();
    QStringList keysOf(const Ref &ref) const;
    // "Ctrl+K: Edit > Copy, My hotkey" for each key used more than once.
    QStringList conflicts() const;
    void setFilter(const QString &text);

    bool apply(QString *error = nullptr);

private:
    void fill();
    void updateButtons();
    void editHotkey(int index); // -1: a new one
    void changeKeyFromEditor();
    QHash<QString, QStringList> keyUsers() const; // key -> names of what uses it
    QString nameOf(const Ref &ref) const;
    void exportHotkeys();
    void importHotkeys();
    void saveList();

    ToolbarStore &m_toolbars;
    QHash<QString, QStringList> m_menuKeys; // id -> keys, for every menu command
    QList<KeyMap::Hotkey> m_hotkeys;
    QHash<QString, QString> m_buttonKeys;   // "bar|index" -> key, for toolbar buttons
    QTreeWidget *m_list;
    QLineEdit *m_filter;
    QKeySequenceEdit *m_keyEdit;
    QLabel *m_conflict;
    QPushButton *m_edit;
    QPushButton *m_duplicate;
    QPushButton *m_delete;
    QPushButton *m_reset;
    QPushButton *m_clear;
    bool m_filling = false;
};
