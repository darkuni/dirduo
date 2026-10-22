#pragma once

#include "ui/FolderTab.h"

#include <QWidget>

class AliasManager;
class BreadcrumbBar;
class QLabel;
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

    // Applied to every tab in this display, and to tabs opened later.
    void setShowHidden(bool show);
    bool showHidden() const { return m_showHidden; }

Q_SIGNALS:
    void activated(FileDisplay *display);
    void currentPathChanged(FileDisplay *display);
    void navigateRequested(FileDisplay *display, const QString &input);
    void renameRequested(FileDisplay *display, const QString &path, const QString &newName);
    void dropRequested(FileDisplay *display, const QList<QUrl> &urls, const QString &targetDir, Qt::DropAction action);
    void openFileRequested(const QString &path);
    void contextMenuRequested(FileDisplay *display, const QPoint &globalPos, bool onItem);
    void tabKeyPressed(FileDisplay *display);

private:
    void wireTab(FolderTab *tab);
    void updateHeader();
    void updateTabText(FolderTab *tab);

    AliasManager &m_aliases;
    Role m_role = Role::None;
    bool m_destLocked = false;
    bool m_showBadge = true;
    bool m_showHidden = false;

    QLabel *m_badge;
    QToolButton *m_back;
    QToolButton *m_forward;
    QToolButton *m_up;
    BreadcrumbBar *m_crumbs;
    QTabBar *m_tabs;
    QStackedWidget *m_stack;
    QLabel *m_status;
};
