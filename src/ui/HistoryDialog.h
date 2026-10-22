#pragma once

#include <QDialog>

class CommandEngine;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTabWidget;
class QTreeWidget;
class QLabel;

// Edit > Undo List... and Tools > File Log: what can be undone this session
// (pick any of them), and every file operation on record, to search when a
// file goes missing.
class HistoryDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Page { Undo, Log };
    HistoryDialog(CommandEngine &engine, Page page, QWidget *parent = nullptr);

    void showPage(Page page);
    QListWidget *undoList() const { return m_undo; }
    QTreeWidget *logTree() const { return m_log; }
    void setLogFilter(const QString &text);
    int visibleLogRows() const;

Q_SIGNALS:
    // An undo happened from here; the lister selects what came back.
    void undone(const QStringList &paths);

private:
    void fillUndo();
    void fillLog();
    void applyFilter();
    void undoSelected();

    CommandEngine &m_engine;
    QTabWidget *m_tabs;
    QListWidget *m_undo;
    QPushButton *m_undoButton;
    QTreeWidget *m_log;
    QLineEdit *m_filter;
    QLabel *m_logStatus;
};
