#pragma once

#include "core/DirLister.h"
#include "core/FileItem.h"
#include "ui/FileListModel.h"
#include "ui/FileSortProxy.h"
#include "ui/FolderFormats.h"

#include <QStringList>
#include <QUrl>
#include <QWidget>

class GroupTreeView;
class QCheckBox;
class QComboBox;
class QLineEdit;
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
    // From the listing, so it also works for remote items.
    bool isFolder(const QString &path) const;
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

    // Columns
    bool isColumnVisible(int column) const;
    void setColumnVisible(int column, bool visible);
    void resetColumns();

    // Grouping
    void setGroupBy(FileListModel::GroupBy g);
    FileListModel::GroupBy groupBy() const;
    void collapseAllGroups();
    void expandAllGroups();
    bool toggleCurrentGroup(); // false when not grouped
    GroupTreeView *detailsView() const { return m_details; }

    // Filter bar
    void setFilterBarVisible(bool visible);
    bool filterBarVisible() const;
    void setFilter(const QString &text, bool invert = false,
                   FileSortProxy::FilterScope scope = FileSortProxy::FilterScope::Files);
    QString filterText() const;

    // Check box mode
    void setCheckMode(bool on);
    bool checkMode() const;

    // Folder sizes for every folder, with bars
    void setLiveFolderSizes(bool on);
    bool liveFolderSizes() const;

    // Flat view
    void setFlatMode(DirLister::FlatMode mode);
    DirLister::FlatMode flatMode() const;
    static QString flatModeName(DirLister::FlatMode mode);
    static bool flatModeFromName(const QString &name, DirLister::FlatMode &out);

    // Folder formats
    FolderFormat currentFormat() const;
    void applyFormat(const FolderFormat &format);
    bool hasOwnFormat() const { return m_formatActive; }
    // After saving a format for this folder: leaving it brings back the
    // default format, or the standard look when there is none.
    void formatSaved();

    void showHeaderMenu(const QPoint &globalPos);

    static QString viewModeName(ViewMode mode);
    static bool viewModeFromName(const QString &name, ViewMode &out);

Q_SIGNALS:
    void pathChanged(const QString &path);
    void viewStateChanged(); // grouping, filter, columns or modes changed
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
    void applyFormatFor(const QString &path);
    void autoSwitchForContent();
    void updateLocationColumn();
    void fitNameColumn();

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
    GroupTreeView *m_details;
    QWidget *m_filterBar;
    QLineEdit *m_filterEdit;
    QCheckBox *m_filterInvert;
    QComboBox *m_filterScope;
    // Folder formats: the look before a folder's own format took over.
    bool m_formatActive = false;
    FolderFormat m_baseFormat;
    bool m_autoIcons = false;
    ViewMode m_modeBeforeAuto = ViewMode::Details;
    FileListModel::GroupBy m_groupBeforeFlat = FileListModel::GroupBy::None;
    QListView *m_list;
    RenameDelegate *m_delegate;     // details view
    RenameDelegate *m_listDelegate; // list and icon views
};
