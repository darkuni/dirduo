#pragma once

#include <QDialog>

class ListerWindow;
class QLabel;
class QLineEdit;
class QListWidget;
class QTreeWidget;
class ToolbarStore;

// Customize mode: while open, the lister's toolbars are editable (drag buttons
// within or between bars, double-click to edit, right-click for more). The
// dialog lists the toolbars (show, hide, new, rename, delete) and ready-made
// buttons that can be dragged onto any bar.
class CustomizeDialog : public QDialog
{
    Q_OBJECT
public:
    CustomizeDialog(ListerWindow *lister, ToolbarStore &store);

    QListWidget *toolbarList() const { return m_bars; }
    QTreeWidget *commandTree() const { return m_commands; }
    // The toolbar picked in the list (the main one when none is).
    QString currentToolbar() const;
    // Adds the ready-made button at index of the command list to the current toolbar.
    void addCatalogButton(int catalogIndex);

private:
    void refreshBars();
    void filterCommands(const QString &text);

    ListerWindow *m_lister;
    ToolbarStore &m_store;
    QListWidget *m_bars;
    QTreeWidget *m_commands;
    QLineEdit *m_search;
    QLabel *m_detail;
};
