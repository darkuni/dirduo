#include "ui/CustomizeDialog.h"

#include "core/CommandCatalog.h"
#include "ui/ButtonBar.h"
#include "ui/ButtonEditor.h"
#include "ui/ListerWindow.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QTabWidget>
#include <QScrollArea>
#include <QJsonObject>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kCatalogRole = Qt::UserRole + 1; // index into CommandCatalog::buttons(), -1 for a blank button

// Toolbar pieces and split buttons, listed under "Toolbar pieces" with
// these negative indexes.
ButtonDef pieceDef(int index)
{
    auto item = [](const char *label, const char *icon, const char *left, const char *enable = "") {
        ButtonDef c;
        c.label = QString::fromUtf8(label);
        c.icon = QString::fromUtf8(icon);
        c.left = QString::fromUtf8(left);
        c.enableWhen = QString::fromUtf8(enable);
        return c;
    };
    ButtonDef d;
    switch (index) {
    case -2: return ButtonDef::makeSpacer();
    case -3: return ButtonDef::makeSeparator();
    case -4: return ButtonDef::makeField(QStringLiteral("filter"));
    case -5: return ButtonDef::makeField(QStringLiteral("find"));
    case -6: return ButtonDef::makeField(QStringLiteral("go"));
    case -7: return ButtonDef::makeField(QStringLiteral("command"));
    case -8:
        d = item("Copy Files", "edit-copy", "Copy", "selection && dest");
        d.tip = QStringLiteral("Copy to the destination (the arrow: other ways to copy)");
        d.right = QStringLiteral("Copy AS");
        d.children = {item("Copy As...", "edit-copy", "Copy AS", "selection && dest"),
                      item("Copy to...", "folder-open", "Copy TO=ask", "selection"),
                      item("Duplicate Here as...", "edit-copy", "Copy AS TO={sourcepath}", "selection"),
                      item("Copy Flat", "view-list-text", "Copy STRUCTURE=flat", "selection && dest"),
                      item("Copy Newer Only", "edit-copy", "Copy WHENEXISTS=newer", "selection && dest")};
        break;
    case -9:
        d = item("Move", "transform-move", "Copy MOVE", "selection && dest");
        d.tip = QStringLiteral("Move to the destination (the arrow: other ways to move)");
        d.right = QStringLiteral("Copy MOVE AS");
        d.children = {item("Move As...", "transform-move", "Copy MOVE AS", "selection && dest"),
                      item("Move to...", "folder-open", "Copy MOVE TO=ask", "selection")};
        break;
    case -10:
        d = item("Delete", "edit-delete", "Delete", "selection");
        d.tip = QStringLiteral("Move to the trash (the arrow: delete for good, clean junk)");
        d.children = {item("Delete Permanently", "edit-delete-shred", "Delete NORECYCLE", "selection"),
                      item("Clean Junk Files", "edit-clear", "Clean")};
        break;
    default:
        return d;
    }
    d.split = true;
    return d;
}

ButtonDef catalogDef(int index)
{
    ButtonDef d;
    if (index <= -2)
        return pieceDef(index);
    if (index < 0 || index >= CommandCatalog::buttons().size()) {
        d.label = QStringLiteral("New Button");
        d.left = QStringLiteral("Go ~");
        return d;
    }
    const CatalogButton &c = CommandCatalog::buttons().at(index);
    d.label = c.label;
    d.icon = c.icon;
    d.tip = c.tip;
    d.left = c.left;
    d.right = c.right;
    d.dynamic = c.dynamic;
    return d;
}

// Ready-made buttons, draggable onto any toolbar in customize mode.
class CatalogTree : public QTreeWidget
{
public:
    using QTreeWidget::QTreeWidget;

protected:
    QStringList mimeTypes() const override { return {QLatin1String(ButtonBar::kMime)}; }
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override
    {
        if (items.isEmpty() || !items.first()->data(0, kCatalogRole).isValid())
            return nullptr;
        QJsonObject payload;
        payload.insert(QStringLiteral("button"), catalogDef(items.first()->data(0, kCatalogRole).toInt()).toJson());
        auto *mime = new QMimeData;
        mime->setData(QLatin1String(ButtonBar::kMime), QJsonDocument(payload).toJson(QJsonDocument::Compact));
        return mime;
    }
};

// One ready-made toolbar's buttons, laid out as a toolbar would be; each can
// be dragged onto a toolbar of yours.
class PresetRow : public QListWidget
{
public:
    PresetRow(const ToolbarDef &def, QWidget *parent)
        : QListWidget(parent)
        , m_def(def)
    {
        setFlow(QListView::LeftToRight);
        setWrapping(true);
        setResizeMode(QListView::Adjust);
        setSpacing(2);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setSelectionMode(QAbstractItemView::SingleSelection);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        for (int i = 0; i < def.buttons.size(); ++i) {
            const ButtonDef &b = def.buttons.at(i);
            QString text = b.label;
            if (b.separator)
                text = QStringLiteral("|");
            else if (b.spacer)
                text = QStringLiteral("[Spacer]");
            else if (!b.field.isEmpty())
                text = QStringLiteral("[%1 box]").arg(b.label);
            else if (b.isMenu())
                text += QStringLiteral(" \u25BE");
            auto *item = new QListWidgetItem(QIcon::fromTheme(b.icon), text, this);
            item->setData(Qt::UserRole, i);
            item->setToolTip(b.isMenu() && !b.isSplit() ? b.tip : (b.tip.isEmpty() ? b.left : b.tip + QStringLiteral("\n") + b.left));
        }
    }
    QSize sizeHint() const override
    {
        // Tall enough for its buttons at the width it has.
        const int w = qMax(200, width());
        int x = 0, rows = 1, rowH = 0;
        for (int i = 0; i < count(); ++i) {
            const QSize s = sizeHintForIndex(model()->index(i, 0)) + QSize(4, 4);
            rowH = qMax(rowH, s.height());
            if (x + s.width() > w - 8 && x > 0) {
                ++rows;
                x = 0;
            }
            x += s.width();
        }
        return QSize(QListWidget::sizeHint().width(), rows * qMax(rowH, 24) + 8);
    }
    void resizeEvent(QResizeEvent *e) override
    {
        QListWidget::resizeEvent(e);
        setFixedHeight(sizeHint().height());
    }

protected:
    QStringList mimeTypes() const override { return {QLatin1String(ButtonBar::kMime)}; }
    QMimeData *mimeData(const QList<QListWidgetItem *> &items) const override
    {
        if (items.isEmpty())
            return nullptr;
        QJsonObject payload;
        payload.insert(QStringLiteral("button"), m_def.buttons.value(items.first()->data(Qt::UserRole).toInt()).toJson());
        auto *mime = new QMimeData;
        mime->setData(QLatin1String(ButtonBar::kMime), QJsonDocument(payload).toJson(QJsonDocument::Compact));
        return mime;
    }

private:
    ToolbarDef m_def;
};

} // namespace

CustomizeDialog::CustomizeDialog(ListerWindow *lister, ToolbarStore &store)
    : QDialog(lister)
    , m_lister(lister)
    , m_store(store)
{
    setWindowTitle(QStringLiteral("Customize Toolbars"));
    setModal(false);
    auto *layout = new QVBoxLayout(this);
    auto *intro = new QLabel(QStringLiteral("While this is open, toolbars are editable: drag buttons to move them "
                                            "(Ctrl+drag copies), double-click one to edit it, right-click for more. "
                                            "Drag buttons from Commands onto any toolbar."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);

    // Toolbars
    auto *barsPage = new QWidget(tabs);
    auto *bl = new QHBoxLayout(barsPage);
    m_bars = new QListWidget(barsPage);
    bl->addWidget(m_bars, 1);
    auto *bb = new QVBoxLayout;
    auto *newBar = new QPushButton(QStringLiteral("New..."), barsPage);
    auto *renameBar = new QPushButton(QStringLiteral("Rename..."), barsPage);
    auto *deleteBar = new QPushButton(QStringLiteral("Delete"), barsPage);
    auto *resetBar = new QPushButton(QStringLiteral("Reset to Default"), barsPage);
    for (QPushButton *b : {newBar, renameBar, deleteBar, resetBar})
        bb->addWidget(b);
    bb->addStretch(1);
    bl->addLayout(bb);
    tabs->addTab(barsPage, QStringLiteral("Toolbars"));

    connect(m_bars, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
        m_lister->setButtonBarVisible(item->data(Qt::UserRole).toString(), item->checkState() == Qt::Checked);
    });
    auto updateButtons = [this, renameBar, deleteBar, resetBar]() {
        const QString id = currentToolbar();
        const bool main = id == QLatin1String(ToolbarStore::kMainId);
        renameBar->setEnabled(!id.isEmpty());
        deleteBar->setEnabled(!id.isEmpty() && !main);
        resetBar->setEnabled(main && m_store.isCustomized(id));
    };
    connect(m_bars, &QListWidget::currentRowChanged, this, updateButtons);
    connect(newBar, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New Toolbar"), QStringLiteral("Name:"),
                                                   QLineEdit::Normal, QStringLiteral("My Buttons"), &ok);
        if (!ok || name.trimmed().isEmpty())
            return;
        const QString id = m_store.create(name);
        refreshBars();
        for (int r = 0; r < m_bars->count(); ++r) {
            if (m_bars->item(r)->data(Qt::UserRole).toString() == id)
                m_bars->setCurrentRow(r);
        }
    });
    connect(renameBar, &QPushButton::clicked, this, [this]() {
        const QString id = currentToolbar();
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Rename Toolbar"), QStringLiteral("Name:"),
                                                   QLineEdit::Normal, m_store.toolbar(id).name, &ok);
        if (ok && m_store.rename(id, name))
            refreshBars();
    });
    connect(deleteBar, &QPushButton::clicked, this, [this]() {
        const QString id = currentToolbar();
        if (QMessageBox::question(this, QStringLiteral("Delete Toolbar"),
                                  QStringLiteral("Delete the toolbar \"%1\" and its buttons?").arg(m_store.toolbar(id).name))
            == QMessageBox::Yes) {
            m_store.remove(id);
            refreshBars();
        }
    });
    connect(resetBar, &QPushButton::clicked, this, [this, updateButtons]() {
        if (QMessageBox::question(this, QStringLiteral("Reset Toolbar"),
                                  QStringLiteral("Put back the standard buttons? Your changes to this toolbar are lost."))
            == QMessageBox::Yes) {
            m_store.resetToDefault(currentToolbar());
            updateButtons();
        }
    });
    connect(&m_store, &ToolbarStore::changed, this, updateButtons);

    // Commands
    auto *cmdPage = new QWidget(tabs);
    auto *cl = new QVBoxLayout(cmdPage);
    m_search = new QLineEdit(cmdPage);
    m_search->setPlaceholderText(QStringLiteral("Search commands"));
    m_search->setClearButtonEnabled(true);
    cl->addWidget(m_search);
    m_commands = new CatalogTree(cmdPage);
    m_commands->setHeaderHidden(true);
    m_commands->setDragEnabled(true);
    m_commands->setDragDropMode(QAbstractItemView::DragOnly);
    m_commands->setRootIsDecorated(true);
    QHash<QString, QTreeWidgetItem *> cats;
    const QList<CatalogButton> &all = CommandCatalog::buttons();
    for (int i = 0; i < all.size(); ++i) {
        const CatalogButton &c = all.at(i);
        QTreeWidgetItem *&cat = cats[c.category];
        if (!cat) {
            cat = new QTreeWidgetItem(m_commands, {c.category});
            cat->setFlags(Qt::ItemIsEnabled);
        }
        auto *item = new QTreeWidgetItem(cat, {c.label});
        item->setIcon(0, QIcon::fromTheme(c.icon));
        item->setData(0, kCatalogRole, i);
        item->setToolTip(0, c.left);
    }
    auto *pieces = new QTreeWidgetItem(m_commands, {QStringLiteral("Toolbar pieces")});
    pieces->setFlags(Qt::ItemIsEnabled);
    const QList<QPair<int, QString>> pieceNames = {
        {-2, QStringLiteral("Spacer (pushes what follows to the far end)")}, {-3, QStringLiteral("Separator")},
        {-4, QStringLiteral("Filter box")}, {-5, QStringLiteral("Find box")}, {-6, QStringLiteral("Go To box")},
        {-7, QStringLiteral("Command box")}, {-8, QStringLiteral("Copy Files \u25BE (split button)")},
        {-9, QStringLiteral("Move \u25BE (split button)")}, {-10, QStringLiteral("Delete \u25BE (split button)")}};
    for (const auto &pn : pieceNames) {
        auto *item = new QTreeWidgetItem(pieces, {pn.second});
        const ButtonDef d = pieceDef(pn.first);
        item->setIcon(0, QIcon::fromTheme(d.icon.isEmpty() ? QStringLiteral("insert-horizontal-rule") : d.icon));
        item->setData(0, kCatalogRole, pn.first);
    }
    auto *blank = new QTreeWidgetItem(m_commands, {QStringLiteral("Blank button (write your own command)")});
    blank->setIcon(0, QIcon::fromTheme(QStringLiteral("list-add")));
    blank->setData(0, kCatalogRole, -1);
    m_commands->expandAll();
    cl->addWidget(m_commands, 1);
    m_detail = new QLabel(cmdPage);
    m_detail->setWordWrap(true);
    m_detail->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_detail->setMinimumHeight(m_detail->fontMetrics().lineSpacing() * 3);
    cl->addWidget(m_detail);
    auto *addHere = new QPushButton(QStringLiteral("Add to Selected Toolbar"), cmdPage);
    cl->addWidget(addHere);
    tabs->addTab(cmdPage, QStringLiteral("Commands"));

    connect(m_search, &QLineEdit::textChanged, this, &CustomizeDialog::filterCommands);
    connect(m_commands, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
        if (!item || !item->data(0, kCatalogRole).isValid()) {
            m_detail->clear();
            return;
        }
        const ButtonDef d = catalogDef(item->data(0, kCatalogRole).toInt());
        QString text = QStringLiteral("<b>%1</b>: %2<br>Left-click: <code>%3</code>")
                           .arg(d.label.toHtmlEscaped(), d.tip.toHtmlEscaped(), d.left.toHtmlEscaped());
        if (!d.right.isEmpty())
            text += QStringLiteral("<br>Right-click: <code>%1</code>").arg(d.right.toHtmlEscaped());
        m_detail->setText(text);
    });
    auto addCurrent = [this]() {
        QTreeWidgetItem *item = m_commands->currentItem();
        if (item && item->data(0, kCatalogRole).isValid())
            addCatalogButton(item->data(0, kCatalogRole).toInt());
    };
    connect(m_commands, &QTreeWidget::itemDoubleClicked, this, addCurrent);
    connect(addHere, &QPushButton::clicked, this, addCurrent);

    // Default toolbars, as in Opus: add one whole, or drag buttons from it.
    auto *presetPage = new QWidget(tabs);
    auto *pl = new QVBoxLayout(presetPage);
    auto *presetIntro = new QLabel(QStringLiteral("Ready-made toolbars. Add one whole, or drag single buttons from them onto your own "
                                                  "toolbars. The Classic Arrangement puts Operations and the drives between the displays, "
                                                  "Favorites beside the drives and Views in the menu bar."),
                                   presetPage);
    presetIntro->setWordWrap(true);
    pl->addWidget(presetIntro);
    auto *opus = new QPushButton(QIcon::fromTheme(QStringLiteral("view-split-top-bottom")), QStringLiteral("Use the Classic Arrangement"),
                                 presetPage);
    opus->setObjectName(QStringLiteral("classic-arrangement"));
    connect(opus, &QPushButton::clicked, this, [this]() { m_lister->runCommand(QStringLiteral("Toolbar ARRANGE=classic")); });
    pl->addWidget(opus, 0, Qt::AlignLeft);
    auto *scroll = new QScrollArea(presetPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *inner = new QWidget(scroll);
    auto *il = new QVBoxLayout(inner);
    for (const ToolbarDef &p : ToolbarStore::presets()) {
        auto *head = new QHBoxLayout;
        auto *title = new QLabel(QStringLiteral("<b>%1</b>").arg(p.name.toHtmlEscaped()), inner);
        head->addWidget(title);
        head->addStretch(1);
        auto *add = new QPushButton(QStringLiteral("Add This Toolbar"), inner);
        add->setObjectName(QStringLiteral("add-preset-") + p.id);
        const QString pid = p.id;
        connect(add, &QPushButton::clicked, this, [this, pid]() {
            const QString id = m_store.addPreset(pid);
            if (id.isEmpty())
                return;
            m_lister->setButtonBarVisible(id, true);
            refreshBars();
            for (int i = 0; i < m_bars->count(); ++i) {
                if (m_bars->item(i)->data(Qt::UserRole).toString() == id)
                    m_bars->setCurrentRow(i);
            }
        });
        head->addWidget(add);
        il->addLayout(head);
        il->addWidget(new PresetRow(p, inner));
    }
    il->addStretch(1);
    scroll->setWidget(inner);
    pl->addWidget(scroll, 1);
    tabs->addTab(presetPage, QStringLiteral("Default Toolbars"));

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(&m_store, &ToolbarStore::added, this, &CustomizeDialog::refreshBars);
    connect(&m_store, &ToolbarStore::removed, this, &CustomizeDialog::refreshBars);
    refreshBars();
    m_bars->setCurrentRow(0);
    resize(680, 600);
}

QString CustomizeDialog::currentToolbar() const
{
    QListWidgetItem *item = m_bars->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QLatin1String(ToolbarStore::kMainId);
}

void CustomizeDialog::refreshBars()
{
    const QString keep = m_bars->currentItem() ? m_bars->currentItem()->data(Qt::UserRole).toString() : QString();
    const QSignalBlocker block(m_bars);
    m_bars->clear();
    for (const QString &id : m_store.ids()) {
        auto *item = new QListWidgetItem(m_store.toolbar(id).name, m_bars);
        item->setData(Qt::UserRole, id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(m_lister->isButtonBarVisible(id) ? Qt::Checked : Qt::Unchecked);
        if (id == keep)
            m_bars->setCurrentItem(item);
    }
}

void CustomizeDialog::filterCommands(const QString &text)
{
    for (int c = 0; c < m_commands->topLevelItemCount(); ++c) {
        QTreeWidgetItem *cat = m_commands->topLevelItem(c);
        if (cat->childCount() == 0) {
            cat->setHidden(!text.isEmpty() && !cat->text(0).contains(text, Qt::CaseInsensitive));
            continue;
        }
        bool any = false;
        for (int i = 0; i < cat->childCount(); ++i) {
            QTreeWidgetItem *item = cat->child(i);
            const bool match = text.isEmpty() || item->text(0).contains(text, Qt::CaseInsensitive)
                || item->toolTip(0).contains(text, Qt::CaseInsensitive);
            item->setHidden(!match);
            any = any || match;
        }
        cat->setHidden(!any);
    }
}

void CustomizeDialog::addCatalogButton(int catalogIndex)
{
    const QString id = currentToolbar();
    ButtonDef d = catalogDef(catalogIndex);
    if (catalogIndex == -1) {
        ButtonEditor editor(d, this);
        if (editor.exec() != QDialog::Accepted)
            return;
        d = editor.button();
    }
    m_store.insertButton(id, int(m_store.toolbar(id).buttons.size()), d);
}
