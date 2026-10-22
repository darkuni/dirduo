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
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {

constexpr int kCatalogRole = Qt::UserRole + 1; // index into CommandCatalog::buttons(), -1 for a blank button

ButtonDef catalogDef(int index)
{
    ButtonDef d;
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

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);

    connect(&m_store, &ToolbarStore::added, this, &CustomizeDialog::refreshBars);
    connect(&m_store, &ToolbarStore::removed, this, &CustomizeDialog::refreshBars);
    refreshBars();
    m_bars->setCurrentRow(0);
    resize(460, 520);
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
    if (catalogIndex < 0) {
        ButtonEditor editor(d, this);
        if (editor.exec() != QDialog::Accepted)
            return;
        d = editor.button();
    }
    m_store.insertButton(id, int(m_store.toolbar(id).buttons.size()), d);
}
