#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QToolBar>
#include <QToolButton>

#include <functional>

class QShortcut;

// A toolbar button. Each mouse button can run its own command, like Opus
// three-button buttons; a button with children is a menu instead.
struct ButtonDef
{
    QString label;
    QString icon;
    QString tip;
    QString left;
    QString right;
    QString middle;
    QString enableWhen; // Condition: grayed out when false
    QString checkWhen;  // Condition: shown pressed when true
    QString key;        // hotkey in the lister, QKeySequence portable text
    QList<ButtonDef> children;
    // A menu that fills itself when opened: favorites, frequent, aliases,
    // collections.
    QString dynamic;
    bool separator = false;
    // Pushes what follows it to the far end of the toolbar.
    bool spacer = false;
    // A menu that is also a button: a click runs left, the arrow opens the menu.
    bool split = false;
    // A text box instead of a button: filter (the source list as you type),
    // find (Find by name on Enter), go (a folder on Enter), or command (left
    // with {text} in it, on Enter). label is the hint shown in it.
    QString field;

    bool isMenu() const { return !children.isEmpty() || !dynamic.isEmpty(); }
    bool isSplit() const { return split && isMenu() && !left.isEmpty(); }
    bool isItem() const { return separator || spacer || !field.isEmpty(); } // not a button
    static QStringList fieldKinds();
    static ButtonDef makeSpacer();
    static ButtonDef makeField(const QString &kind);
    static QStringList dynamicKinds();
    QJsonObject toJson() const;
    static ButtonDef fromJson(const QJsonObject &o);
    static ButtonDef makeSeparator();
    bool operator==(const ButtonDef &o) const;
};

struct ToolbarDef
{
    QString id;   // file name in ~/.config/dirduo/toolbars, without .json
    QString name; // title shown on the toolbar and in menus
    QList<ButtonDef> buttons;
    // How its buttons show: auto (icon and label, label alone without an
    // icon), icons, text, beside, under. iconSize 0: the standard size.
    QString style = QStringLiteral("auto");
    int iconSize = 0;
    static QStringList styles();
};

// Every toolbar the user has, shared by all listers: editing one in customize
// mode changes it in every window. Each lives in toolbars/<id>.json; the
// standard "buttons" toolbar has no file until it is first edited, so new
// default buttons reach anyone who has not changed it.
class ToolbarStore : public QObject
{
    Q_OBJECT
public:
    explicit ToolbarStore(const QString &dir, QObject *parent = nullptr);

    static constexpr const char *kMainId = "buttons";

    QStringList ids() const;
    bool has(const QString &id) const { return m_bars.contains(id); }
    ToolbarDef toolbar(const QString &id) const { return m_bars.value(id); }

    void setButtons(const QString &id, const QList<ButtonDef> &buttons);
    void insertButton(const QString &id, int index, const ButtonDef &def);
    void replaceButton(const QString &id, int index, const ButtonDef &def);
    void removeButton(const QString &id, int index);
    void moveButton(const QString &id, int from, int to); // to: index before the move

    QString create(const QString &name); // returns the new id
    bool rename(const QString &id, const QString &name);
    void setStyle(const QString &id, const QString &style, int iconSize);
    bool remove(const QString &id);      // not the main toolbar
    void resetToDefault(const QString &id);
    bool isCustomized(const QString &id) const;
    // Reads the files again (after a settings restore).
    void reload();

    QString fileFor(const QString &id) const;
    static QList<ButtonDef> defaultButtons();
    // Ready-made toolbars to add whole or take buttons from (Customize >
    // Default Toolbars), as Opus has: Operations, Navigation, Views...
    static QList<ToolbarDef> presets();
    static ToolbarDef preset(const QString &id);
    // Adds a copy of a ready-made toolbar; returns its id. If it was added
    // before and not renamed, that one's id (nothing new is made).
    QString addPreset(const QString &presetId);
    static QList<ButtonDef> loadFile(const QString &file, QString *name, bool *ok, ToolbarDef *style = nullptr);
    static bool saveFile(const QString &file, const QString &name, const QList<ButtonDef> &buttons, const ToolbarDef *style = nullptr);

Q_SIGNALS:
    void changed(const QString &id);
    void added(const QString &id);
    void removed(const QString &id);

private:
    void load();
    void save(const QString &id);

    QString m_dir;
    QMap<QString, ToolbarDef> m_bars;
};

class ButtonBar;
class QLineEdit;

class ThreeButton : public QToolButton
{
    Q_OBJECT
public:
    // bar is the editable bar the button belongs to, if any (not the drives).
    ThreeButton(const ButtonDef &def, QWidget *parent, ButtonBar *bar = nullptr);
    const ButtonDef &definition() const { return m_def; }
    // The toolbar's style ("auto", "icons", "text", "beside", "under").
    void applyStyle(const QString &style);

Q_SIGNALS:
    void commandTriggered(const QString &command);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    QSize sizeHint() const override;

private:
    QMenu *buildMenu(const QList<ButtonDef> &children, QWidget *parent);
    bool editing() const;
    bool ownArrow() const;
    int arrowWidth() const;
    bool onArrow(const QPoint &pos) const;

    ButtonDef m_def;
    ButtonBar *m_bar;
    Qt::MouseButton m_pressed = Qt::NoButton;
    QPoint m_pressPos;
    bool m_holdMenuShown = false; // a long press opened the choices: the release runs nothing
};

// A toolbar of ButtonDefs from the store. In customize mode its buttons stop
// running commands: drag them within the bar or to another bar, double-click
// to edit, right-click for more.
class ButtonBar : public QToolBar
{
    Q_OBJECT
public:
    // Hotkeys of the buttons are attached to shortcutHost (the lister).
    ButtonBar(ToolbarStore &store, const QString &id, QWidget *shortcutHost, QWidget *parent = nullptr);

    QString id() const { return m_id; }
    ToolbarStore &store() { return m_store; }
    QList<QWidget *> items() const { return m_items; }
    ThreeButton *buttonAt(int index) const;
    int indexOf(QWidget *item) const { return int(m_items.indexOf(item)); }

    void setEditMode(bool on);
    bool editMode() const { return m_edit; }

    // Fills a dynamic menu (favorites, frequent, ...) as it opens.
    using MenuProvider = std::function<void(QMenu *menu, const QString &kind)>;
    void setMenuProvider(MenuProvider p) { m_menuProvider = std::move(p); }
    void fillDynamicMenu(QMenu *menu, const QString &kind) const;

    using Evaluator = std::function<bool(const QString &condition, bool fallback)>;
    void setEvaluator(Evaluator eval) { m_eval = std::move(eval); }
    bool evaluate(const QString &condition, bool fallback) const;
    void updateStates();
    // What a filter field shows (the source list's filter), kept in step.
    using FieldProvider = std::function<QString(const QString &kind)>;
    void setFieldProvider(FieldProvider p) { m_fieldProvider = std::move(p); }
    QLineEdit *fieldAt(int index) const;
    bool hasSpacer() const;

    // Customize mode actions; also used by tests.
    void editButton(int index);
    void showEditMenu(int index, const QPoint &globalPos); // index -1: empty space
    void startDrag(int index);
    int dropIndexAt(const QPoint &pos) const;
    // The button that takes files dropped at pos (bar coordinates).
    ThreeButton *fileTargetAt(const QPoint &pos) const;

    static constexpr const char *kMime = "application/x-dirduo-button";

Q_SIGNALS:
    void commandTriggered(const QString &command);
    void filesDropped(const QString &command, const QStringList &paths);
    // A field: typed in (filter) or Enter pressed (the rest).
    void fieldEdited(const QString &kind, const QString &text, const QString &command, bool entered);
    void fillChanged(); // a spacer came or went
    void customizeRequested();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void paintEvent(QPaintEvent *event) override;

private:
    void rebuild();

    ToolbarStore &m_store;
    QString m_id;
    QPointer<QWidget> m_host;
    QList<QWidget *> m_items;
    QList<QShortcut *> m_shortcuts;
    bool m_edit = false;
    int m_dropIndex = -1;
    Evaluator m_eval;
    MenuProvider m_menuProvider;
    FieldProvider m_fieldProvider;
};
