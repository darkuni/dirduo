#pragma once

#include "core/FileItem.h"

#include <QStringList>
#include <QUrl>
#include <QWidget>

class DirLister;
class FileListModel;
class FileSortProxy;
class QAbstractItemView;
class QItemSelectionModel;
class QListView;
class QStackedLayout;
class QTreeView;
class RenameDelegate;

// One tab of a file display: a folder, its history, and the view showing it.
class FolderTab : public QWidget
{
    Q_OBJECT
public:
    enum class ViewMode { Details, List, Icons };

    explicit FolderTab(QWidget *parent = nullptr);

    void navigate(const QString &path, bool addToHistory = true);
    bool back();
    bool forward();
    bool up();
    void refresh();
    bool canGoBack() const { return !m_back.isEmpty(); }
    bool canGoForward() const { return !m_forward.isEmpty(); }

    QString path() const { return m_path; }
    QString title() const;
    QStringList selectedPaths() const;
    QString focusedPath() const;
    QString statusText() const;
    QString lastError() const { return m_error; }

    // Selects these paths once they are listed (now, or after the next refresh).
    void selectPaths(const QStringList &paths);
    void selectAll();
    void selectNone();
    void invertSelection();
    int selectPattern(const QString &wildcard); // returns how many matched

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return m_mode; }
    void setShowHidden(bool show);
    bool showHidden() const;

    bool beginRename();
    bool isRenaming() const;

    QAbstractItemView *view() const;
    void focusView();

    QByteArray headerState() const;
    void restoreHeaderState(const QByteArray &state);

    static QString viewModeName(ViewMode mode);
    static bool viewModeFromName(const QString &name, ViewMode &out);

Q_SIGNALS:
    void pathChanged(const QString &path);
    void activated();                 // clicked or focused
    void selectionChanged();
    void statusChanged();
    void renameRequested(const QString &path, const QString &newName);
    void dropRequested(const QList<QUrl> &urls, const QString &targetDir, Qt::DropAction action);
    void openFileRequested(const QString &path);
    void openInNewTabRequested(const QString &path);
    void contextMenuRequested(const QPoint &globalPos, bool onItem);
    void tabKeyPressed(bool backwards);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onListed(const QString &path, const FileItemList &items, bool isRefresh);
    void onFailed(const QString &path, const QString &error);
    void applyPendingSelection();
    void activateIndex(const QModelIndex &proxyIndex);
    void setupView(QAbstractItemView *v);
    void travelRename(int delta);

    QString m_path;
    QString m_error;
    QStringList m_back;
    QStringList m_forward;
    QStringList m_pendingSelection;
    QString m_pendingFocusAfterUp;
    ViewMode m_mode = ViewMode::Details;

    DirLister *m_lister;
    FileListModel *m_model;
    FileSortProxy *m_proxy;
    QItemSelectionModel *m_selection;
    QStackedLayout *m_stack;
    QTreeView *m_details;
    QListView *m_list;
    RenameDelegate *m_delegate;     // details view
    RenameDelegate *m_listDelegate; // list and icon views
};
