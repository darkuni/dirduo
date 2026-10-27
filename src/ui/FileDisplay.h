#pragma once

#include "ui/FolderTab.h"

#include <QWidget>

#include <functional>

class AliasManager;
class FolderTree;
class QSplitter;
class BreadcrumbBar;
class QLabel;
class StatusLine;
class QMenu;
class QStackedWidget;
class QTabBar;
class QToolButton;

// One side of a lister: header (SOURCE/DEST badge, back/forward/up,
// breadcrumbs), tabs, and the status line.
class FileDisplay : public QWidget
{
    Q_OBJECT
public:
    enum class Role { None, Source, Dest };

    FileDisplay(AliasManager &aliases, QWidget *parent = nullptr);

    FolderTab *currentTab() const;
    FolderTab *tab(int index) const;
    int tabCount() const;
    int currentIndex() const;
    void setCurrentIndex(int index);

    FolderTab *addTab(const QString &path, bool makeCurrent = true);
    void closeTab(int index);
    void cycleTab(int delta);

    void setRole(Role role);
    Role role() const { return m_role; }
    void setDestLocked(bool locked);
    bool destLocked() const { return m_destLocked; }
    void setShowRoleBadge(bool show);

    BreadcrumbBar *breadcrumb() const { return m_crumbs; }
    // Fills the favorites menu of the location bar's star.
    void setMenuProvider(std::function<void(QMenu *, const QString &)> p) { m_menuProvider = std::move(p); }
    QWidget *locationBar() const { return m_header; }
    QToolButton *refreshButton() const { return m_refresh; }
    QToolButton *homeButton() const { return m_home; }
    QToolButton *favoritesButton() const { return m_star; }
    QToolButton *recentButton() const { return m_recent; }
    QToolButton *copyPathButton() const { return m_copyPath; }
    QToolButton *swapPathsButton() const { return m_swapPaths; }
    void fillRecentMenu(QMenu *menu);
    // Folders visited in any display, newest first (kept between runs).
    static QStringList history();
    static void recordVisit(const QString &path);
    // The Home button's folder: your home folder unless set to another.
    static QString homeFolder();
    void setHomeFolder(const QString &path); // empty: back to your home folder

    // This display's own folder tree (Set TREE=separate).
    void setTreeVisible(bool visible);
    bool treeVisible() const;
    FolderTree *tree() const { return m_tree; }
    QByteArray bodyState() const;
    void restoreBodyState(const QByteArray &state);

    // Applied to every tab in this display, and to tabs opened later.
    void setShowHidden(bool show);
    bool showHidden() const { return m_showHidden; }
    // Every tab and the tree, in percent.
    void setZoom(int percent);
    int zoom() const { return m_zoom; }
    QString freeSpaceText() const; // the right of the status bar: "12.3 GB free of 64 GB", empty for servers
    QString statusLineText() const; // the left of the status bar
    // The lister's own status bar definition (empty: Preferences'), for every tab.
    void setStatusDefinition(const QString &def);
    QString statusDefinition() const { return m_statusDef; }

Q_SIGNALS:
    void activated(FileDisplay *display);
    void currentPathChanged(FileDisplay *display);
    void navigateRequested(FileDisplay *display, const QString &input);
    void renameRequested(FileDisplay *display, const QString &path, const QString &newName);
    void dropRequested(FileDisplay *display, const QList<QUrl> &urls, const QString &targetDir, Qt::DropAction action);
    void openFileRequested(const QString &path);
    void contextMenuRequested(FileDisplay *display, const QPoint &globalPos, bool onItem);
    void tabKeyPressed(FileDisplay *display);
    void zoomRequested(FileDisplay *display, int steps);
    void commandRequested(FileDisplay *display, const QString &command);
    // This display's folder, opened in the other display.
    void copyPathRequested(FileDisplay *display);
    // Selection, view or folder changed: buttons may need to update.
    void stateChanged(FileDisplay *display);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void wireTab(FolderTab *tab);
    void updateHeader();
    void updateTabText(FolderTab *tab);
    void applyStyle();

    AliasManager &m_aliases;
    Role m_role = Role::None;
    bool m_destLocked = false;
    bool m_showBadge = true;
    int m_zoom = 100;
    bool m_showHidden = false;

    QWidget *m_accent; // colored edge: source, dest or locked dest
    QSplitter *m_body;
    FolderTree *m_tree = nullptr;
    QLabel *m_badge;
    QToolButton *m_back;
    QToolButton *m_forward;
    QToolButton *m_up;
    QToolButton *m_refresh = nullptr;
    QToolButton *m_home = nullptr;
    QToolButton *m_star = nullptr;
    QToolButton *m_recent = nullptr;
    QToolButton *m_copyPath = nullptr;
    QToolButton *m_swapPaths = nullptr;
    QLabel *m_folderIcon = nullptr;
    QWidget *m_header = nullptr;
    std::function<void(QMenu *, const QString &)> m_menuProvider;
    BreadcrumbBar *m_crumbs;
    QTabBar *m_tabs;
    QStackedWidget *m_stack;
    StatusLine *m_status;
    void updateFreeSpace();
    QString m_statusDef;
};
