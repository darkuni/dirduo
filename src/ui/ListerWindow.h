#pragma once

#include "core/CommandEngine.h"

#include <QMainWindow>
#include <QPointer>

class AppContext;
class ButtonBar;
class DrivesBar;
class FileDisplay;
class FolderTab;
class QAction;
class QActionGroup;
class QMenu;
class QSettings;
class QSplitter;
class QToolBar;
class ToolbarStrip;

// A lister: a window with one or two file displays. In dual mode one display
// is SOURCE and the other DEST, and every command acts source to dest.
// Commands reach the lister through the CommandContext interface.
class ListerWindow : public QMainWindow, public CommandContext
{
    Q_OBJECT
public:
    explicit ListerWindow(AppContext &app, QWidget *parent = nullptr);

    void openDefault(const QString &path);
    bool restoreListerState(QSettings &s);
    void saveListerState(QSettings &s) const;

    bool runCommand(const QString &script);

    FileDisplay *sourceDisplay() const;
    FileDisplay *destDisplay() const; // null in single mode
    FileDisplay *display(int index) const { return m_displays[index]; }
    bool isDual() const { return m_dual; }

    enum class ToolbarPlace { Top, Bottom, Left, Right, Between };
    // Moves a toolbar to an edge of the window or between the displays.
    // newRow starts a new row (or column) instead of sharing the last one.
    void placeToolbar(QToolBar *bar, ToolbarPlace place, bool newRow = false);
    ToolbarPlace toolbarPlace(QToolBar *bar) const;
    QList<QToolBar *> toolbars() const;
    QToolBar *toolbarByName(const QString &name) const;
    DrivesBar *drivesBar() const { return m_drives; }
    ToolbarStrip *toolbarStrip() const { return m_strip; }

    // CommandContext
    QString sourcePath() const override;
    QString destPath() const override;
    QStringList selectedPaths() const override;
    QString focusedPath() const override;
    bool promptText(const QString &title, const QString &label, QString &value) override;
    bool confirm(const QString &title, const QString &text) override;
    void showError(const QString &title, const QString &text) override;
    QString chooseDestination(const QString &title) override;
    bool go(const ParsedCommand &cmd, QString *error) override;
    bool set(const ParsedCommand &cmd, QString *error) override;
    bool select(const ParsedCommand &cmd, QString *error) override;
    bool beginInlineRename() override;
    bool toolbar(const ParsedCommand &cmd, QString *error) override;
    void afterLocalChange(const QStringList &selectPaths) override;

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void buildActions();
    void buildMenus();
    void buildToolbar();
    void setupToolbarMenu(QToolBar *bar);
    void fillToolbarMenu(QMenu *menu, QToolBar *bar);
    void rebuildToolbarsMenu();

    void setDual(bool on);
    void setSideBySide(bool sideBySide);
    void makeSource(FileDisplay *d);
    void updateRoles();
    void updateTitle();
    void syncViewActions();
    void mirrorNavigation(FileDisplay *from, const QString &oldPath, const QString &newPath);

    void onNavigateRequested(FileDisplay *d, const QString &input);
    void onRenameRequested(FileDisplay *d, const QString &path, const QString &newName);
    void onDropRequested(FileDisplay *d, const QList<QUrl> &urls, const QString &targetDir, Qt::DropAction action);
    void onContextMenu(FileDisplay *d, const QPoint &globalPos, bool onItem);
    void openFile(const QString &path);
    void rebuildAliasMenu();

    QAction *commandAction(const QString &text, const QString &command, const QKeySequence &key = {},
                           const QString &icon = {});
    FolderTab *sourceTab() const;

    AppContext &m_app;
    QSplitter *m_split;
    FileDisplay *m_displays[2];
    int m_source = 0;
    bool m_dual = true;
    bool m_sideBySide = true;
    bool m_navLock = false;
    bool m_mirroring = false;
    QString m_lastPath[2]; // for navigation lock

    ButtonBar *m_buttons = nullptr;
    DrivesBar *m_drives = nullptr;
    ToolbarStrip *m_strip = nullptr;
    QMenu *m_toolbarsMenu = nullptr;

    QPointer<FolderTab> m_changeTarget; // tab that afterLocalChange refers to

    QAction *m_actSingle = nullptr;
    QAction *m_actDualSide = nullptr;
    QAction *m_actDualStacked = nullptr;
    QAction *m_actDetails = nullptr;
    QAction *m_actList = nullptr;
    QAction *m_actIcons = nullptr;
    QAction *m_actHidden = nullptr;
    QAction *m_actLock = nullptr;
    QAction *m_actNavLock = nullptr;
    QMenu *m_aliasMenu = nullptr;
};
