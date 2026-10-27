#pragma once

#include "core/DirLister.h"
#include "core/FileItem.h"
#include "ui/FileListModel.h"
#include "ui/FileSortProxy.h"
#include "ui/FolderFormats.h"

#include <QStringList>
#include <QUrl>
#include <QHash>
#include <QWidget>

#include <functional>

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
    enum class ViewMode { Details, List, Icons, Thumbnails };

    explicit FolderTab(QWidget *parent = nullptr);

    void navigate(const QString &path, bool addToHistory = true);
    bool back();
    bool forward();
    bool up();
    void refresh();
    bool canGoBack() const { return !m_back.isEmpty(); }
    QStringList backHistory() const { return m_back; }       // oldest first
    QStringList forwardHistory() const { return m_forward; }
    bool canGoForward() const { return !m_forward.isEmpty(); }

    QString path() const { return m_path; }
    QString title() const;
    QStringList selectedPaths() const;
    // Every item listed, in the order shown (after sorting and the filter).
    QStringList visiblePaths() const;
    QString focusedPath() const;
    // From the listing, so it also works for remote items.
    bool isFolder(const QString &path) const;
    QString statusText() const;
    // An error or "Reading folder...", which shows instead of the definition.
    QString statusMessage() const;
    // The values of the status bar's codes for this list (StatusFormat).
    QHash<QString, QString> statusValues(bool withDisk) const;
    QString lastError() const { return m_error; }

    // Selects these paths once they are listed (now, or after the next refresh).
    void selectPaths(const QStringList &paths);
    void selectAll();
    void selectNone();
    void invertSelection();
    int selectPattern(const QString &wildcard); // returns how many matched
    // Selects (or adds, or takes out) every listed item that passes; returns
    // how many passed.
    enum class SelectMode { Replace, Add, Remove };
    int selectWhere(const std::function<bool(const FileItem &)> &test, SelectMode mode = SelectMode::Replace);

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
    FileListModel *fileModel() const { return m_model; }

    // Find-as-you-type: typing in the list opens a field at the bottom. Plain
    // text jumps to a name, wildcards select, ">" runs a command, "/" or "~"
    // goes to an alias or path; Ctrl+F turns it into the filter bar.
    void openTypeAhead(const QString &text);
    void closeTypeAhead();
    bool typeAheadOpen() const;
    QLineEdit *typeAheadEdit() const { return m_faytEdit; }
    QString typeAheadMode() const;

    // Expandable folders (details view): a folder's contents open under it.
    bool expandFolder(const QString &path);
    void collapseFolder(const QString &path);
    void toggleFolder(const QString &path);
    void collapseAllFolders();
    bool isFolderExpanded(const QString &path) const { return m_children.contains(path); }
    QStringList expandedFolders() const { return m_children.keys(); }

    // The lister's zoom, in percent of the normal text and icon size.
    void setZoom(int percent);
    int zoom() const { return m_zoom; }
    // Does the name column fill the room left over (not sized by hand)?
    bool nameColumnFills() const;
    // Size in pixels at this text size and zoom.
    int zoomed(int px) const;

    // Filter bar
    void setFilterBarVisible(bool visible);
    bool filterBarVisible() const;
    void setFilter(const QString &text, bool invert = false,
                   FileSortProxy::FilterScope scope = FileSortProxy::FilterScope::Files);
    QString filterText() const;
    bool filterHides() const;
    FileSortProxy::FilterScope filterScope() const;

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

    // The listing as shown (visible columns in their order, grouping, flat
    // view), for Print Folder.
    enum class ExportFormat { Text, Csv, Html };
    QString exportListing(ExportFormat format, bool selectedOnly) const;

    static QString viewModeName(ViewMode mode);
    static bool viewModeFromName(const QString &name, ViewMode &out);

Q_SIGNALS:
    // Shift+wheel or a pinch over the list: steps in (+) or out (-).
    void zoomRequested(int steps);
    void expandFailed(const QString &path, const QString &error);
    // From the type-ahead field: run this in the lister.
    void commandRequested(const QString &command);
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
    static QStyle *headerGripStyle();
    void applySize();

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
    int m_zoom = 100;
    bool m_nameFills = true;    // the name column takes the room left over
    bool m_fittingName = false; // sizing it ourselves
    bool m_headerDragging = false;
    QHash<QString, DirLister *> m_children; // one per expanded folder
    QWidget *m_fayt = nullptr;
    QLineEdit *m_faytEdit = nullptr;
    class QLabel *m_faytLabel = nullptr;
    void updateTypeAhead();
    void typeAheadStep(int delta); // next or previous match
    bool typeAheadKey(QKeyEvent *ke);
    QList<QModelIndex> typeAheadMatches() const;
    void updateExpandable();
    bool expanderHit(const QPoint &viewportPos, QModelIndex *index) const;
    int m_wheelZoom = 0;     // wheel units toward the next step (trackpads send small ones)
    qreal m_pinchZoom = 1.0; // pinch scale toward the next step
    ViewMode m_modeBeforeAuto = ViewMode::Details;
    FileListModel::GroupBy m_groupBeforeFlat = FileListModel::GroupBy::None;
    QListView *m_list;
    RenameDelegate *m_delegate;     // details view
    RenameDelegate *m_listDelegate; // list and icon views
};
