#include "ui/ButtonBar.h"

#include "ui/ButtonEditor.h"
#include "ui/Style.h"

#include <QApplication>
#include <QDir>
#include <QDrag>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QSaveFile>
#include <QShortcut>
#include <QStyleOptionToolButton>
#include <QUrl>

namespace {

QString describe(const ButtonDef &d)
{
    QStringList lines;
    if (!d.tip.isEmpty())
        lines << d.tip;
    if (d.isMenu())
        return lines.join(QLatin1Char('\n'));
    auto add = [&](const char *which, const QString &cmd) {
        if (!cmd.isEmpty())
            lines << QStringLiteral("%1: %2").arg(QLatin1String(which), cmd.split(QLatin1Char('\n')).join(QStringLiteral("; ")));
    };
    add("Left-click", d.left);
    add("Right-click", d.right);
    add("Middle-click", d.middle);
    if (!d.key.isEmpty())
        lines << QStringLiteral("Hotkey: %1").arg(QKeySequence(d.key, QKeySequence::PortableText).toString(QKeySequence::NativeText));
    return lines.join(QLatin1Char('\n'));
}

// A separator that can be dragged and deleted in customize mode.
class SeparatorItem : public QWidget
{
public:
    SeparatorItem(ButtonBar *bar)
        : QWidget(bar)
        , m_bar(bar)
    {
        setFixedSize(10, 22);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        QColor c = palette().color(QPalette::WindowText);
        c.setAlphaF(0.25);
        p.setPen(c);
        if (m_bar->orientation() == Qt::Horizontal)
            p.drawLine(width() / 2, 3, width() / 2, height() - 4);
        else
            p.drawLine(3, height() / 2, width() - 4, height() / 2);
        if (m_bar->editMode()) {
            QPen dash(palette().color(QPalette::Highlight));
            dash.setStyle(Qt::DotLine);
            p.setPen(dash);
            p.drawRect(rect().adjusted(0, 0, -1, -1));
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        m_press = e->position().toPoint();
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (m_bar->editMode() && (e->buttons() & Qt::LeftButton)
            && (e->position().toPoint() - m_press).manhattanLength() >= QApplication::startDragDistance())
            m_bar->startDrag(m_bar->indexOf(this));
    }
    void contextMenuEvent(QContextMenuEvent *e) override
    {
        if (m_bar->editMode())
            m_bar->showEditMenu(m_bar->indexOf(this), e->globalPos());
        else
            e->ignore();
    }

private:
    ButtonBar *m_bar;
    QPoint m_press;
};

} // namespace

// ---------------------------------------------------------------- ButtonDef

QJsonObject ButtonDef::toJson() const
{
    QJsonObject o;
    if (separator) {
        o.insert(QStringLiteral("separator"), true);
        return o;
    }
    o.insert(QStringLiteral("label"), label);
    auto put = [&o](const char *k, const QString &v) {
        if (!v.isEmpty())
            o.insert(QLatin1String(k), v);
    };
    put("icon", icon);
    put("tip", tip);
    if (!isMenu())
        o.insert(QStringLiteral("left"), left);
    put("dynamic", dynamic);
    put("right", right);
    put("middle", middle);
    put("enableWhen", enableWhen);
    put("checkWhen", checkWhen);
    put("key", key);
    if (!children.isEmpty()) {
        QJsonArray arr;
        for (const ButtonDef &c : children)
            arr.append(c.toJson());
        o.insert(QStringLiteral("menu"), arr);
    }
    return o;
}

ButtonDef ButtonDef::fromJson(const QJsonObject &o)
{
    ButtonDef d;
    d.separator = o.value(QStringLiteral("separator")).toBool();
    if (d.separator)
        return d;
    d.label = o.value(QStringLiteral("label")).toString();
    d.icon = o.value(QStringLiteral("icon")).toString();
    d.tip = o.value(QStringLiteral("tip")).toString();
    d.left = o.value(QStringLiteral("left")).toString();
    d.right = o.value(QStringLiteral("right")).toString();
    d.middle = o.value(QStringLiteral("middle")).toString();
    d.enableWhen = o.value(QStringLiteral("enableWhen")).toString();
    d.checkWhen = o.value(QStringLiteral("checkWhen")).toString();
    d.key = o.value(QStringLiteral("key")).toString();
    d.dynamic = o.value(QStringLiteral("dynamic")).toString().toLower();
    if (!dynamicKinds().contains(d.dynamic))
        d.dynamic.clear();
    const QJsonArray menu = o.value(QStringLiteral("menu")).toArray();
    for (const QJsonValue &v : menu) {
        const ButtonDef c = fromJson(v.toObject());
        if (c.separator || !c.label.isEmpty())
            d.children << c;
    }
    return d;
}

QStringList ButtonDef::dynamicKinds()
{
    return {QStringLiteral("favorites"), QStringLiteral("frequent"), QStringLiteral("aliases"), QStringLiteral("collections"),
            QStringLiteral("servers")};
}

ButtonDef ButtonDef::makeSeparator()
{
    ButtonDef d;
    d.separator = true;
    return d;
}

bool ButtonDef::operator==(const ButtonDef &o) const
{
    return toJson() == o.toJson();
}

// ---------------------------------------------------------------- ToolbarStore

ToolbarStore::ToolbarStore(const QString &dir, QObject *parent)
    : QObject(parent)
    , m_dir(dir)
{
    load();
}

QString ToolbarStore::fileFor(const QString &id) const
{
    return QDir(m_dir).filePath(id + QStringLiteral(".json"));
}

QList<ButtonDef> ToolbarStore::loadFile(const QString &file, QString *name, bool *ok)
{
    if (ok)
        *ok = false;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    if (name)
        *name = doc.object().value(QStringLiteral("name")).toString();
    QList<ButtonDef> out;
    const QJsonArray arr = doc.object().value(QStringLiteral("buttons")).toArray();
    for (const QJsonValue &v : arr) {
        const ButtonDef d = ButtonDef::fromJson(v.toObject());
        if (d.separator || !d.label.isEmpty())
            out << d;
    }
    if (ok)
        *ok = true;
    return out;
}

bool ToolbarStore::saveFile(const QString &file, const QString &name, const QList<ButtonDef> &buttons)
{
    QJsonArray arr;
    for (const ButtonDef &d : buttons)
        arr.append(d.toJson());
    QJsonObject root;
    root.insert(QStringLiteral("name"), name);
    root.insert(QStringLiteral("buttons"), arr);
    QDir().mkpath(QFileInfo(file).absolutePath());
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

void ToolbarStore::load()
{
    m_bars.clear();
    const QDir dir(m_dir);
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        QString id = QFileInfo(f).completeBaseName();
        if (id == QLatin1String("default"))
            continue; // phase 1 name of the main toolbar, read below
        QString name;
        bool ok = false;
        const QList<ButtonDef> buttons = loadFile(dir.filePath(f), &name, &ok);
        if (!ok)
            continue;
        m_bars.insert(id, ToolbarDef{id, name.isEmpty() ? id : name, buttons});
    }
    if (!m_bars.contains(QLatin1String(kMainId))) {
        bool ok = false;
        QList<ButtonDef> buttons = loadFile(dir.filePath(QStringLiteral("default.json")), nullptr, &ok);
        if (!ok)
            buttons = defaultButtons();
        m_bars.insert(QLatin1String(kMainId), ToolbarDef{QLatin1String(kMainId), QStringLiteral("Buttons"), buttons});
    }
}

void ToolbarStore::save(const QString &id)
{
    const ToolbarDef &d = m_bars[id];
    saveFile(fileFor(id), d.name, d.buttons);
}

QStringList ToolbarStore::ids() const
{
    QStringList out{QLatin1String(kMainId)};
    QList<ToolbarDef> rest;
    for (const ToolbarDef &d : m_bars) {
        if (d.id != QLatin1String(kMainId))
            rest << d;
    }
    std::sort(rest.begin(), rest.end(), [](const ToolbarDef &a, const ToolbarDef &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    for (const ToolbarDef &d : rest)
        out << d.id;
    return out;
}

void ToolbarStore::setButtons(const QString &id, const QList<ButtonDef> &buttons)
{
    if (!m_bars.contains(id))
        return;
    m_bars[id].buttons = buttons;
    save(id);
    Q_EMIT changed(id);
}

void ToolbarStore::insertButton(const QString &id, int index, const ButtonDef &def)
{
    QList<ButtonDef> b = m_bars.value(id).buttons;
    b.insert(qBound(0, index, int(b.size())), def);
    setButtons(id, b);
}

void ToolbarStore::replaceButton(const QString &id, int index, const ButtonDef &def)
{
    QList<ButtonDef> b = m_bars.value(id).buttons;
    if (index < 0 || index >= b.size())
        return;
    b[index] = def;
    setButtons(id, b);
}

void ToolbarStore::removeButton(const QString &id, int index)
{
    QList<ButtonDef> b = m_bars.value(id).buttons;
    if (index < 0 || index >= b.size())
        return;
    b.removeAt(index);
    setButtons(id, b);
}

void ToolbarStore::moveButton(const QString &id, int from, int to)
{
    QList<ButtonDef> b = m_bars.value(id).buttons;
    if (from < 0 || from >= b.size())
        return;
    const ButtonDef d = b.takeAt(from);
    if (to > from)
        --to; // the gap it left
    b.insert(qBound(0, to, int(b.size())), d);
    setButtons(id, b);
}

QString ToolbarStore::create(const QString &name)
{
    QString base;
    for (const QChar c : name.toLower()) {
        if (c.isLetterOrNumber())
            base += c;
        else if (!base.endsWith(QLatin1Char('-')))
            base += QLatin1Char('-');
    }
    while (base.endsWith(QLatin1Char('-')))
        base.chop(1);
    if (base.isEmpty() || base == QLatin1String("drives") || base == QLatin1String("default"))
        base = QStringLiteral("toolbar");
    QString id = base;
    for (int n = 2; m_bars.contains(id); ++n)
        id = base + QString::number(n);
    m_bars.insert(id, ToolbarDef{id, name.trimmed().isEmpty() ? id : name.trimmed(), {}});
    save(id);
    Q_EMIT added(id);
    return id;
}

bool ToolbarStore::rename(const QString &id, const QString &name)
{
    if (!m_bars.contains(id) || name.trimmed().isEmpty())
        return false;
    m_bars[id].name = name.trimmed();
    save(id);
    Q_EMIT changed(id);
    return true;
}

bool ToolbarStore::remove(const QString &id)
{
    if (id == QLatin1String(kMainId) || !m_bars.contains(id))
        return false;
    m_bars.remove(id);
    QFile::remove(fileFor(id));
    Q_EMIT removed(id);
    return true;
}

void ToolbarStore::resetToDefault(const QString &id)
{
    if (id != QLatin1String(kMainId))
        return;
    QFile::remove(fileFor(id));
    QFile::remove(QDir(m_dir).filePath(QStringLiteral("default.json")));
    m_bars[id].buttons = defaultButtons();
    m_bars[id].name = QStringLiteral("Buttons");
    Q_EMIT changed(id);
}

void ToolbarStore::reload()
{
    const QStringList before = m_bars.keys();
    load();
    for (const QString &id : before) {
        if (!m_bars.contains(id))
            Q_EMIT removed(id);
    }
    for (const QString &id : m_bars.keys()) {
        if (before.contains(id))
            Q_EMIT changed(id);
        else
            Q_EMIT added(id);
    }
}

bool ToolbarStore::isCustomized(const QString &id) const
{
    return QFileInfo::exists(fileFor(id))
        || (id == QLatin1String(kMainId) && QFileInfo::exists(QDir(m_dir).filePath(QStringLiteral("default.json"))));
}

QList<ButtonDef> ToolbarStore::defaultButtons()
{
    auto b = [](const char *label, const char *icon, const char *tip, const char *left, const char *right = "",
                const char *middle = "") {
        ButtonDef d;
        d.label = QString::fromUtf8(label);
        d.icon = QString::fromUtf8(icon);
        d.tip = QString::fromUtf8(tip);
        d.left = QString::fromUtf8(left);
        d.right = QString::fromUtf8(right);
        d.middle = QString::fromUtf8(middle);
        return d;
    };
    ButtonDef sep;
    sep.separator = true;
    QList<ButtonDef> list = {
        b("Copy", "edit-copy", "Copy the selection to the destination", "Copy", "Copy AS"),
        b("Move", "transform-move", "Move the selection to the destination", "Copy MOVE", "Copy MOVE AS"),
        b("Delete", "edit-delete", "Delete the selection", "Delete", "Delete NORECYCLE"),
        b("Rename", "edit-rename", "Rename in place; arrow keys move to the next item (right-click: Advanced Rename)", "Rename",
          "Rename ADVANCED"),
        b("New Folder", "folder-new", "Create folders (separate several with commas)", "CreateFolder"),
        sep,
        b("Swap", "view-split-left-right", "Swap the folders in the two displays", "Go SWAP"),
        b("Same", "go-jump", "Open the source folder in the destination", "Go {sourcepath} OPENINDEST"),
        b("Vertical", "view-split-left-right", "Dual vertical: displays side by side (right-click: single display)",
          "Set DUAL=vertical", "Set DUAL=off"),
        b("Horizontal", "view-split-top-bottom", "Dual horizontal: displays top and bottom (right-click: single display)",
          "Set DUAL=horizontal", "Set DUAL=off"),
        b("Lock Dest", "object-locked", "Keep the destination where it is", "Set DESTLOCK=toggle"),
        b("Hidden", "view-hidden", "Show or hide hidden files", "Set HIDDEN=toggle"),
        sep,
        b("Tree", "view-list-tree", "Folder tree: shared by the lister (right-click: one per display)",
          "Set TREE=toggle", "Set TREE=separate"),
        b("Flat", "view-list-details", "Flat view: everything under this folder, grouped by folder (right-click: mixed)",
          "Set FLATVIEW=toggle", "Set FLATVIEW=mixed"),
        b("Group", "view-group", "Group by type (right-click: no grouping)", "Set GROUPBY=type", "Set GROUPBY=none"),
        b("Filter", "view-filter", "Filter the list as you type (Ctrl+F)", "Set FILTERBAR=toggle"),
        b("Sizes", "folder-stats", "Calculate the size of every folder", "Set FOLDERSIZES=toggle"),
        b("Theme", "color-management", "Next theme (Linopus Light, Dark, Workbench...)", "Set THEME=next"),
    };
    // Grayed out when there is nothing to act on; pressed while switched on.
    const QHash<QString, QPair<QString, QString>> states = {
        {QStringLiteral("Copy"), {QStringLiteral("selection && dest"), {}}},
        {QStringLiteral("Move"), {QStringLiteral("selection && dest"), {}}},
        {QStringLiteral("Delete"), {QStringLiteral("selection"), {}}},
        {QStringLiteral("Rename"), {QStringLiteral("selection"), {}}},
        {QStringLiteral("Swap"), {QStringLiteral("dual"), {}}},
        {QStringLiteral("Same"), {QStringLiteral("dual"), {}}},
        {QStringLiteral("Vertical"), {{}, QStringLiteral("layout=vertical")}},
        {QStringLiteral("Horizontal"), {{}, QStringLiteral("layout=horizontal")}},
        {QStringLiteral("Lock Dest"), {QStringLiteral("dual"), QStringLiteral("destlocked")}},
        {QStringLiteral("Hidden"), {{}, QStringLiteral("hidden")}},
        {QStringLiteral("Tree"), {{}, QStringLiteral("tree")}},
        {QStringLiteral("Flat"), {{}, QStringLiteral("flat")}},
        {QStringLiteral("Group"), {{}, QStringLiteral("grouped")}},
        {QStringLiteral("Filter"), {{}, QStringLiteral("filter")}},
        {QStringLiteral("Sizes"), {{}, QStringLiteral("foldersizes")}},
    };
    // Favorites as a menu that keeps itself up to date (Ctrl+D adds).
    ButtonDef favs;
    favs.label = QStringLiteral("Favorites");
    favs.icon = QStringLiteral("bookmarks");
    favs.tip = QStringLiteral("Your favorite folders (Ctrl+D adds the one you are in)");
    favs.dynamic = QStringLiteral("favorites");
    list << favs;
    for (ButtonDef &d : list) {
        const auto it = states.constFind(d.label);
        if (it != states.cend()) {
            d.enableWhen = it->first;
            d.checkWhen = it->second;
        }
    }
    return list;
}


// ---------------------------------------------------------------- ThreeButton

// Buttons for some commands offer their choices on a long press (the click
// still runs the button's own function): Flat view's kinds.
static QList<ButtonDef> holdMenuFor(const ButtonDef &def)
{
    QList<ButtonDef> out;
    if (def.isMenu() || !def.left.trimmed().startsWith(QLatin1String("Set FLATVIEW"), Qt::CaseInsensitive))
        return out;
    auto item = [](const char *label, const char *cmd, const char *check) {
        ButtonDef d;
        d.label = QString::fromLatin1(label);
        d.left = QString::fromLatin1(cmd);
        d.checkWhen = QString::fromLatin1(check);
        return d;
    };
    out << item("Off", "Set FLATVIEW=off", "!flat") << item("Grouped by Folder", "Set FLATVIEW=grouped", "flatgrouped")
        << item("Ungrouped", "Set FLATVIEW=ungrouped", "flatungrouped")
        << item("Ungrouped, Files Only", "Set FLATVIEW=nofolders", "flatfiles");
    return out;
}

ThreeButton::ThreeButton(const ButtonDef &def, QWidget *parent, ButtonBar *bar)
    : QToolButton(parent)
    , m_def(def)
    , m_bar(bar)
{
    setText(def.label);
    const QIcon icon = QIcon::fromTheme(def.icon);
    if (!icon.isNull()) {
        setIcon(icon);
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    } else {
        setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    setToolTip(describe(def));
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    setCheckable(!def.checkWhen.isEmpty());
    if (!def.dynamic.isEmpty()) {
        auto *menu = new QMenu(this);
        menu->setToolTipsVisible(true);
        const QString kind = def.dynamic;
        connect(menu, &QMenu::aboutToShow, this, [this, menu, kind]() {
            menu->clear();
            if (m_bar)
                m_bar->fillDynamicMenu(menu, kind);
        });
        setMenu(menu);
        setPopupMode(QToolButton::InstantPopup);
    } else if (def.isMenu()) {
        setMenu(buildMenu(def.children, this));
        setPopupMode(QToolButton::InstantPopup);
    } else if (const QList<ButtonDef> hold = holdMenuFor(def); !hold.isEmpty()) {
        // Press and hold (or a long touch) for the choices; a click still runs it.
        QMenu *menu = buildMenu(hold, this);
        connect(menu, &QMenu::aboutToShow, this, [this]() { m_holdMenuShown = true; });
        setMenu(menu);
        setPopupMode(QToolButton::DelayedPopup);
    }
    // Files dropped on a button are handled by the bar (a grayed-out button
    // takes no events, but Copy should still take dropped files).
    setAcceptDrops(false);
}

bool ThreeButton::editing() const
{
    return m_bar && m_bar->editMode();
}

QMenu *ThreeButton::buildMenu(const QList<ButtonDef> &children, QWidget *parent)
{
    auto *menu = new QMenu(parent);
    for (const ButtonDef &c : children) {
        if (c.separator) {
            menu->addSeparator();
            continue;
        }
        if (c.isMenu()) {
            QMenu *sub = buildMenu(c.children, menu);
            sub->setTitle(c.label);
            sub->setIcon(QIcon::fromTheme(c.icon));
            menu->addMenu(sub);
            continue;
        }
        QAction *a = menu->addAction(QIcon::fromTheme(c.icon), c.label);
        a->setToolTip(describe(c));
        a->setCheckable(!c.checkWhen.isEmpty());
        const QString cmd = c.left;
        const QString enable = c.enableWhen;
        const QString check = c.checkWhen;
        connect(a, &QAction::triggered, this, [this, cmd]() {
            if (!editing())
                Q_EMIT commandTriggered(cmd);
        });
        connect(menu, &QMenu::aboutToShow, a, [this, a, enable, check]() {
            if (!m_bar)
                return;
            a->setEnabled(m_bar->evaluate(enable, true));
            if (a->isCheckable())
                a->setChecked(m_bar->evaluate(check, false));
        });
    }
    menu->setToolTipsVisible(true);
    return menu;
}

void ThreeButton::mousePressEvent(QMouseEvent *event)
{
    if (editing()) {
        m_pressPos = event->position().toPoint();
        event->accept();
        return;
    }
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        if (m_def.isMenu()) {
            event->ignore();
            return;
        }
        m_pressed = event->button();
        setDown(true);
        event->accept();
        return;
    }
    m_pressed = Qt::LeftButton;
    m_holdMenuShown = false;
    QToolButton::mousePressEvent(event);
}

void ThreeButton::mouseMoveEvent(QMouseEvent *event)
{
    if (editing()) {
        if ((event->buttons() & Qt::LeftButton)
            && (event->position().toPoint() - m_pressPos).manhattanLength() >= QApplication::startDragDistance())
            m_bar->startDrag(m_bar->indexOf(this));
        return;
    }
    QToolButton::mouseMoveEvent(event);
}

void ThreeButton::mouseReleaseEvent(QMouseEvent *event)
{
    if (editing()) {
        event->accept();
        return;
    }
    const bool inside = rect().contains(event->position().toPoint());
    const Qt::MouseButton button = event->button();
    if (button == Qt::RightButton || button == Qt::MiddleButton) {
        setDown(false);
        event->accept();
        if (inside && button == m_pressed) {
            const QString cmd = button == Qt::RightButton ? m_def.right : m_def.middle;
            Q_EMIT commandTriggered(cmd.isEmpty() ? m_def.left : cmd);
        }
        m_pressed = Qt::NoButton;
        return;
    }
    const bool wasChecked = isChecked();
    QToolButton::mouseReleaseEvent(event);
    if (isCheckable())
        setChecked(wasChecked); // the condition decides, not the click
    if (inside && button == Qt::LeftButton && m_pressed == Qt::LeftButton && !m_def.left.isEmpty() && !m_def.isMenu()
        && !m_holdMenuShown)
        Q_EMIT commandTriggered(m_def.left);
    m_holdMenuShown = false;
    m_pressed = Qt::NoButton;
}

void ThreeButton::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (editing()) {
        m_bar->editButton(m_bar->indexOf(this));
        event->accept();
        return;
    }
    QToolButton::mouseDoubleClickEvent(event);
}

void ThreeButton::contextMenuEvent(QContextMenuEvent *event)
{
    if (editing())
        m_bar->showEditMenu(m_bar->indexOf(this), event->globalPos());
    else
        event->accept(); // right-click is a function, not a menu
}

void ThreeButton::paintEvent(QPaintEvent *event)
{
    QToolButton::paintEvent(event);
    if (editing()) {
        QPainter p(this);
        QPen dash(palette().color(QPalette::Highlight));
        dash.setStyle(Qt::DotLine);
        p.setPen(dash);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
    }
}

// ---------------------------------------------------------------- ButtonBar

ButtonBar::ButtonBar(ToolbarStore &store, const QString &id, QWidget *shortcutHost, QWidget *parent)
    : QToolBar(store.toolbar(id).name, parent)
    , m_store(store)
    , m_id(id)
    , m_host(shortcutHost)
{
    setObjectName(QStringLiteral("toolbar-") + id);
    setMovable(true);
    setIconSize(QSize(Style::instance()->scaled(18), Style::instance()->scaled(18)));
    connect(Style::instance(), &Style::changed, this, [this]() {
        setIconSize(QSize(Style::instance()->scaled(18), Style::instance()->scaled(18)));
    });
    setAcceptDrops(true);
    connect(&m_store, &ToolbarStore::changed, this, [this](const QString &changedId) {
        if (changedId == m_id)
            rebuild();
    });
    rebuild();
}

ThreeButton *ButtonBar::buttonAt(int index) const
{
    return qobject_cast<ThreeButton *>(m_items.value(index));
}

void ButtonBar::rebuild()
{
    setWindowTitle(m_store.toolbar(m_id).name);
    clear();
    for (QWidget *w : std::as_const(m_items))
        w->deleteLater();
    m_items.clear();
    qDeleteAll(m_shortcuts);
    m_shortcuts.clear();

    for (const ButtonDef &def : m_store.toolbar(m_id).buttons) {
        if (def.separator) {
            auto *sep = new SeparatorItem(this);
            addWidget(sep);
            m_items << sep;
            continue;
        }
        auto *b = new ThreeButton(def, this, this);
        connect(b, &ThreeButton::commandTriggered, this, &ButtonBar::commandTriggered);
        addWidget(b);
        m_items << b;
        if (!def.key.isEmpty() && m_host && !def.isMenu()) {
            auto *sc = new QShortcut(QKeySequence(def.key, QKeySequence::PortableText), m_host);
            sc->setContext(Qt::WindowShortcut);
            const QString cmd = def.left;
            connect(sc, &QShortcut::activated, this, [this, b, cmd]() {
                if (!m_edit && b->isEnabled())
                    Q_EMIT commandTriggered(cmd);
            });
            m_shortcuts << sc;
        }
    }
    updateStates();
}

void ButtonBar::setEditMode(bool on)
{
    m_edit = on;
    for (QWidget *w : std::as_const(m_items)) {
        if (on)
            w->setEnabled(true); // grayed-out buttons can still be edited
        w->update();
    }
    if (!on)
        updateStates();
}

void ButtonBar::fillDynamicMenu(QMenu *menu, const QString &kind) const
{
    if (m_menuProvider)
        m_menuProvider(menu, kind);
    if (menu->isEmpty())
        menu->addAction(QStringLiteral("(empty)"))->setEnabled(false);
}

bool ButtonBar::evaluate(const QString &condition, bool fallback) const
{
    if (condition.trimmed().isEmpty())
        return fallback;
    return m_eval ? m_eval(condition, fallback) : fallback;
}

void ButtonBar::updateStates()
{
    if (m_edit)
        return;
    for (QWidget *w : std::as_const(m_items)) {
        auto *b = qobject_cast<ThreeButton *>(w);
        if (!b)
            continue;
        b->setEnabled(evaluate(b->definition().enableWhen, true));
        if (b->isCheckable())
            b->setChecked(evaluate(b->definition().checkWhen, false));
    }
}

void ButtonBar::editButton(int index)
{
    const QList<ButtonDef> buttons = m_store.toolbar(m_id).buttons;
    if (index < 0 || index >= buttons.size() || buttons.at(index).separator)
        return;
    ButtonEditor editor(buttons.at(index), this);
    if (editor.exec() == QDialog::Accepted)
        m_store.replaceButton(m_id, index, editor.button());
}

void ButtonBar::showEditMenu(int index, const QPoint &globalPos)
{
    QMenu menu(this);
    const QList<ButtonDef> buttons = m_store.toolbar(m_id).buttons;
    const bool onItem = index >= 0 && index < buttons.size();
    const int after = onItem ? index + 1 : int(buttons.size());
    if (onItem && !buttons.at(index).separator) {
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("document-edit")), QStringLiteral("Edit...")),
                &QAction::triggered, this, [this, index]() { editButton(index); });
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Duplicate")),
                &QAction::triggered, this, [this, index, buttons]() {
                    m_store.insertButton(m_id, index + 1, buttons.at(index));
                });
    }
    connect(menu.addAction(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("New Button...")),
            &QAction::triggered, this, [this, after]() {
                ButtonDef d;
                d.label = QStringLiteral("New Button");
                ButtonEditor editor(d, this);
                if (editor.exec() == QDialog::Accepted)
                    m_store.insertButton(m_id, after, editor.button());
            });
    connect(menu.addAction(QStringLiteral("Add Separator")), &QAction::triggered, this,
            [this, after]() { m_store.insertButton(m_id, after, ButtonDef::makeSeparator()); });
    if (onItem) {
        menu.addSeparator();
        connect(menu.addAction(QIcon::fromTheme(QStringLiteral("edit-delete")), QStringLiteral("Delete")),
                &QAction::triggered, this, [this, index]() { m_store.removeButton(m_id, index); });
    }
    menu.exec(globalPos);
}

void ButtonBar::startDrag(int index)
{
    const QList<ButtonDef> buttons = m_store.toolbar(m_id).buttons;
    if (index < 0 || index >= buttons.size())
        return;
    QJsonObject payload;
    payload.insert(QStringLiteral("toolbar"), m_id);
    payload.insert(QStringLiteral("index"), index);
    payload.insert(QStringLiteral("button"), buttons.at(index).toJson());
    auto *mime = new QMimeData;
    mime->setData(QLatin1String(kMime), QJsonDocument(payload).toJson(QJsonDocument::Compact));
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    if (QWidget *w = m_items.value(index))
        drag->setPixmap(w->grab());
    drag->exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
}

int ButtonBar::dropIndexAt(const QPoint &pos) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        const QRect g = m_items.at(i)->geometry();
        if (orientation() == Qt::Horizontal ? pos.x() < g.center().x() : pos.y() < g.center().y())
            return i;
    }
    return int(m_items.size());
}

ThreeButton *ButtonBar::fileTargetAt(const QPoint &pos) const
{
    for (QWidget *w : m_items) {
        auto *b = qobject_cast<ThreeButton *>(w);
        if (b && b->geometry().contains(pos) && !b->definition().isMenu() && !b->definition().left.isEmpty())
            return b;
    }
    return nullptr;
}

void ButtonBar::dragEnterEvent(QDragEnterEvent *event)
{
    if (!m_edit && event->mimeData()->hasUrls()) {
        // Accept over the whole bar; the move event decides per button.
        event->acceptProposedAction();
        return;
    }
    if (m_edit && event->mimeData()->hasFormat(QLatin1String(kMime))) {
        event->acceptProposedAction();
        m_dropIndex = dropIndexAt(event->position().toPoint());
        update();
        return;
    }
    event->ignore();
}

void ButtonBar::dragMoveEvent(QDragMoveEvent *event)
{
    if (!m_edit && event->mimeData()->hasUrls()) {
        if (fileTargetAt(event->position().toPoint()))
            event->acceptProposedAction();
        else
            event->ignore();
        return;
    }
    if (!m_edit || !event->mimeData()->hasFormat(QLatin1String(kMime))) {
        event->ignore();
        return;
    }
    event->acceptProposedAction();
    const int i = dropIndexAt(event->position().toPoint());
    if (i != m_dropIndex) {
        m_dropIndex = i;
        update();
    }
}

void ButtonBar::dragLeaveEvent(QDragLeaveEvent *)
{
    m_dropIndex = -1;
    update();
}

void ButtonBar::dropEvent(QDropEvent *event)
{
    if (!m_edit && event->mimeData()->hasUrls()) {
        ThreeButton *b = fileTargetAt(event->position().toPoint());
        QStringList paths;
        for (const QUrl &u : event->mimeData()->urls())
            paths << (u.isLocalFile() ? u.toLocalFile() : u.toString());
        if (!b || paths.isEmpty()) {
            event->ignore();
            return;
        }
        event->acceptProposedAction();
        Q_EMIT filesDropped(b->definition().left, paths);
        return;
    }
    if (!m_edit || !event->mimeData()->hasFormat(QLatin1String(kMime))) {
        event->ignore();
        return;
    }
    const int to = dropIndexAt(event->position().toPoint());
    m_dropIndex = -1;
    update();
    const QJsonObject payload = QJsonDocument::fromJson(event->mimeData()->data(QLatin1String(kMime))).object();
    const ButtonDef def = ButtonDef::fromJson(payload.value(QStringLiteral("button")).toObject());
    if (!def.separator && def.label.isEmpty())
        return;
    const QString from = payload.value(QStringLiteral("toolbar")).toString();
    const int fromIndex = payload.value(QStringLiteral("index")).toInt(-1);
    // Ctrl while dropping copies instead of moving, as in a file manager.
    const bool copy = from.isEmpty() || event->modifiers().testFlag(Qt::ControlModifier);
    if (!copy && from == m_id) {
        m_store.moveButton(m_id, fromIndex, to);
    } else {
        m_store.insertButton(m_id, to, def);
        if (!copy && m_store.has(from))
            m_store.removeButton(from, fromIndex);
    }
    event->setDropAction(copy ? Qt::CopyAction : Qt::MoveAction);
    event->accept();
}

void ButtonBar::paintEvent(QPaintEvent *event)
{
    QToolBar::paintEvent(event);
    if (m_dropIndex < 0)
        return;
    QPainter p(this);
    QPen pen(palette().color(QPalette::Highlight));
    pen.setWidth(3);
    p.setPen(pen);
    int at;
    if (m_dropIndex < m_items.size())
        at = orientation() == Qt::Horizontal ? m_items.at(m_dropIndex)->geometry().left() - 1
                                             : m_items.at(m_dropIndex)->geometry().top() - 1;
    else if (!m_items.isEmpty())
        at = orientation() == Qt::Horizontal ? m_items.last()->geometry().right() + 2
                                             : m_items.last()->geometry().bottom() + 2;
    else
        at = 8;
    if (orientation() == Qt::Horizontal)
        p.drawLine(at, 3, at, height() - 4);
    else
        p.drawLine(3, at, width() - 4, at);
}
