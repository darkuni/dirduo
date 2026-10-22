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
class QLineEdit;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

// Synchronize: compare two folders, show what would happen to every item that
// differs, let any action be changed, then queue the copies and deletions as
// ordinary file operations.
class SyncDialog : public QDialog
{
    Q_OBJECT
public:
    SyncDialog(CommandEngine &engine, const QString &left, const QString &right, const SyncOptions &options,
               QWidget *parent = nullptr);
    ~SyncDialog() override;

    void startCompare();
    bool isComparing() const { return m_busy; }
    QList<SyncEntry> entries() const { return m_entries; }
    void setAction(int entryIndex, SyncEntry::Action action);
    // Queues the plan; returns the number of jobs started.
    int synchronize();

    // For tests.
    QTreeWidget *tree() const { return m_tree; }
    QCheckBox *showSameBox() const { return m_showSame; }
    QPushButton *syncButton() const { return m_sync; }
    QString summary() const;

private:
    SyncOptions options() const;
    void fill();
    void updateRow(QTreeWidgetItem *row);
    void updateSummary();
    void showRowMenu(const QPoint &pos);

    CommandEngine &m_engine;
    QList<SyncEntry> m_entries;
    QString m_comparedLeft;
    QString m_comparedRight;
    std::shared_ptr<ScanProgress> m_progress;
    QFutureWatcher<QList<SyncEntry>> m_watcher;
    QTimer m_tick;
    bool m_busy = false; // until the results are in the list

    QLineEdit *m_left;
    QLineEdit *m_right;
    QComboBox *m_mode;
    QComboBox *m_compare;
    QLineEdit *m_exclude;
    QPushButton *m_compareButton;
    QTreeWidget *m_tree;
    QCheckBox *m_showSame;
    QLabel *m_status;
    QPushButton *m_sync;
};
