#pragma once

#include "core/CommandEngine.h"

#include <QMainWindow>
#include <QPointer>

#include <optional>

class AppContext;
class ButtonBar;
class CommandConsole;
class CustomizeDialog;
class DrivesBar;
class FileDisplay;
class FolderTree;
class FolderTab;
class QAction;
class QActionGroup;
class QMenu;
class QSettings;
class QSplitter;
class QToolBar;
class ToolbarStrip;
class ViewerPane;

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
    // Runs with paths standing in for the selection (files dropped on a button).
    bool runCommandWithFiles(const QString &script, const QStringList &paths);
    // Runs without error boxes; the error, if any, comes back in *error.
    bool runCommandReporting(const QString &script, QString *error);

    // Toolbars made of buttons (the store's toolbars, shown in this lister).
    QList<ButtonBar *> buttonBars() const;
    ButtonBar *buttonBar(const QString &id) const { return m_buttonBars.value(id); }
    void setButtonBarVisible(const QString &id, bool visible);
    bool isButtonBarVisible(const QString &id) const;
    void customize();       // opens customize mode
    bool isCustomizing() const;
    CustomizeDialog *customizeDialog() const;
    void showConsole();
    CommandConsole *console() const;
    // A button condition ("selection && dual") evaluated here and now.
    bool evaluateCondition(const QString &condition, bool fallback, QString *error = nullptr) const;
    void updateButtonStates();
    // Opens (or refreshes) the tab showing a Find result collection.
    void showFindResults(const QString &collectionLocation, int count);
    // Favourites, frequent folders, aliases or collections, as menu entries.
    void fillDynamicMenu(QMenu *menu, const QString &kind);
    ViewerPane *viewer() const { return m_viewer; }

    FileDisplay *sourceDisplay() const;
    // This lister's zoom over the app's text size, in percent (Shift+wheel).
    int zoom() const { return m_zoom; }
    void setZoom(int percent);
    void zoomBy(int steps, bool showLevel = false);
    static QList<int> zoomLevels();
    FileDisplay *destDisplay() const; // null in single mode
    FileDisplay *display(int index) const { return m_displays[index]; }
    bool isDual() const { return m_dual; }

    enum class ToolbarPlace { Top, Bottom, Left, Right, Between };
    // Off; one tree for the lister that follows the source; or one per display.
    enum class TreeMode { Off, Shared, Separate };
    TreeMode treeMode() const { return m_treeMode; }
    void setTreeMode(TreeMode mode);
    FolderTree *sharedTree() const { return m_sharedTree; }
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
    bool printFolder(const ParsedCommand &cmd, QString *error) override;
    bool settings(const ParsedCommand &cmd, QString *error) override;
    bool tool(const ParsedCommand &cmd, QString *error) override;
    bool imageCommand(const ParsedCommand &cmd, QString *error);
    bool advancedRename(const QStringList &paths, const RenameOptions &initial, QString *error) override;
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
    void createButtonBar(const QString &id, bool show);
    void removeButtonBar(const QString &id);
    void scheduleButtonStates();
    std::optional<bool> conditionAtom(const QString &name, const QString &value) const;
    void fillViewMenus();

    QAction *commandAction(const QString &text, const QString &command, const QKeySequence &key = {},
                           const QString &icon = {});
    FolderTab *sourceTab() const;

    AppContext &m_app;
    QSplitter *m_outer; // shared tree | displays
    QSplitter *m_split;
    FolderTree *m_sharedTree = nullptr;
    TreeMode m_treeMode = TreeMode::Off;
    TreeMode m_lastTreeMode = TreeMode::Shared; // what TREE=toggle brings back
    FileDisplay *m_displays[2];
    int m_source = 0;
    bool m_dual = true;
    bool m_sideBySide = true;
    bool m_navLock = false;
    bool m_mirroring = false;
    QString m_lastPath[2]; // for navigation lock

    ButtonBar *m_buttons = nullptr; // the main toolbar
    QMap<QString, ButtonBar *> m_buttonBars;
    QPointer<CustomizeDialog> m_customize;
    QPointer<CommandConsole> m_console;
    bool m_statesPending = false;
    bool m_hasSelectionOverride = false;
    QStringList m_selectionOverride;
    QString *m_errorSink = nullptr;
    QStringList m_lastFindRoots;
    ViewerPane *m_viewer = nullptr;
    void updateViewer();
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
    QAction *m_actThumbs = nullptr;
    QAction *m_actHidden = nullptr;
    QAction *m_actLock = nullptr;
    QAction *m_actNavLock = nullptr;
    QMenu *m_aliasMenu = nullptr;
    QAction *m_actFilterBar = nullptr;
    QAction *m_actChecks = nullptr;
    QAction *m_actFolderSizes = nullptr;
    QAction *m_actAutoFormat = nullptr;
    QMenu *m_treeMenu = nullptr;
    QMenu *m_flatMenu = nullptr;
    QMenu *m_groupMenu = nullptr;
    QMenu *m_themeMenu = nullptr;
    QMenu *m_sizeMenu = nullptr;
    QAction *m_actZoomReset = nullptr;
    class MetadataPane *m_metaPane = nullptr;
public:
    MetadataPane *metadataPane() const { return m_metaPane; }
private:
    int m_zoom = 100;
};
