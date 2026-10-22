#pragma once

#include "core/FolderSync.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>

#include <memory>

class CommandEngine;
class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTreeWidget;

// Finds files that are the same (by contents, or by name), grouped; check the
// copies to get rid of and send them to the trash. One copy of each group is
// always left unchecked.
class DuplicatesDialog : public QDialog
{
    Q_OBJECT
public:
    DuplicatesDialog(CommandEngine &engine, const QStringList &folders, const DuplicateOptions &options,
                     QWidget *parent = nullptr);
    ~DuplicatesDialog() override;

    void startSearch();
    bool isSearching() const { return m_busy; }
    QList<DuplicateGroup> groups() const { return m_groups; }

    enum class Keep { Newest, Oldest, First, ShortestPath };
    void checkAllBut(Keep keep);
    void uncheckAll();
    QStringList checkedPaths() const;
    // Sends the checked files to the trash; returns how many.
    int trashChecked();

    QTreeWidget *tree() const { return m_tree; }
    QString summary() const;

Q_SIGNALS:
    void showRequested(const QString &path); // double-click: show it in the lister

private:
    DuplicateOptions options() const;
    void fill();
    void updateSummary();

    CommandEngine &m_engine;
    QList<DuplicateGroup> m_groups;
    std::shared_ptr<ScanProgress> m_progress;
    QFutureWatcher<QList<DuplicateGroup>> m_watcher;
    QTimer m_tick;
    bool m_filling = false;
    bool m_busy = false; // until the results are in the list

    QListWidget *m_folders;
    QComboBox *m_by;
    QSpinBox *m_minSize;
    QCheckBox *m_hidden;
    QPushButton *m_find;
    QTreeWidget *m_tree;
    QLabel *m_status;
    QPushButton *m_trash;
};
