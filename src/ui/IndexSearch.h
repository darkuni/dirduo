#pragma once

#include "core/FileIndex.h"

#include <QAbstractTableModel>
#include <QFutureWatcher>
#include <QPointer>
#include <QTimer>
#include <QWidget>

#include <atomic>
#include <memory>

class QLabel;
class QLineEdit;
class QTreeView;
class ListerWindow;

class IndexResultsModel : public QAbstractTableModel
{
public:
    using QAbstractTableModel::QAbstractTableModel;
    void setHits(const QList<FileIndex::Hit> &hits);
    const FileIndex::Hit &hit(int row) const { return m_hits.at(row); }
    QList<FileIndex::Hit> hits() const { return m_hits; }
    int rowCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : int(m_hits.size()); }
    int columnCount(const QModelIndex &parent = {}) const override { return parent.isValid() ? 0 : 4; }
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation o, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    QStringList mimeTypes() const override;

private:
    QList<FileIndex::Hit> m_hits;
};

// Index Search: type, and every file and folder in the index with that in its
// name shows at once, as Everything does. Enter goes to it in the lister
// (Ctrl+Enter opens it), drag results into a lister, or put them all in a tab.
class IndexSearch : public QWidget
{
    Q_OBJECT
public:
    static IndexSearch *open(ListerWindow *lister, const QString &query = {});
    static IndexSearch *current();

    void setQuery(const QString &q);
    QString query() const;
    // Tests: waits for the search under way.
    bool waitResults(int ms);
    IndexResultsModel *results() const { return m_model; }
    int total() const { return m_total; }
    QTreeView *view() const { return m_view; }
    QString statusText() const;
    void goToRow(int row);   // in the lister, selected
    void openRow(int row);   // in its application
    bool showAllInTab();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    explicit IndexSearch(ListerWindow *lister);
    void runSearch();
    void updateStatus();
    void contextMenu(const QPoint &pos);

    QPointer<ListerWindow> m_lister;
    QLineEdit *m_edit;
    QTreeView *m_view;
    IndexResultsModel *m_model;
    QLabel *m_status;
    QTimer m_debounce;
    QFutureWatcher<FileIndex::Result> m_watcher;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    QString m_running;
    bool m_again = false;
    int m_total = 0;
    bool m_truncated = false;
};
