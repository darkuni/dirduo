#include "ui/FavoritesDialog.h"

#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
constexpr int kPathRole = Qt::UserRole + 1;
constexpr int kKindRole = Qt::UserRole + 2; // 0 entry, 1 group, 2 separator
}

FavoritesDialog::FavoritesDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Organize Favorites"));
    auto *layout = new QVBoxLayout(this);
    auto *row = new QHBoxLayout;
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Folder")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    row->addWidget(m_tree, 1);
    auto *buttons = new QVBoxLayout;
    auto *group = new QPushButton(QStringLiteral("New Group"), this);
    auto *sep = new QPushButton(QStringLiteral("Add Separator"), this);
    auto *rename = new QPushButton(QStringLiteral("Rename"), this);
    auto *remove = new QPushButton(QStringLiteral("Remove"), this);
    for (QPushButton *b : {group, sep, rename, remove})
        buttons->addWidget(b);
    buttons->addStretch(1);
    row->addLayout(buttons);
    layout->addLayout(row, 1);
    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &FavoritesDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(box);

    connect(group, &QPushButton::clicked, this, [this]() {
        auto *g = new QTreeWidgetItem(m_tree, {QStringLiteral("New Group")});
        g->setData(0, kKindRole, 1);
        g->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-favorites")));
        g->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
        m_tree->setCurrentItem(g);
        m_tree->editItem(g, 0);
    });
    connect(sep, &QPushButton::clicked, this, [this]() {
        auto *s = new QTreeWidgetItem(m_tree, {QStringLiteral("――――")});
        s->setData(0, kKindRole, 2);
        s->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
    });
    connect(rename, &QPushButton::clicked, this, [this]() {
        if (QTreeWidgetItem *i = m_tree->currentItem(); i && i->data(0, kKindRole).toInt() != 2)
            m_tree->editItem(i, 0);
    });
    connect(remove, &QPushButton::clicked, this, [this]() { delete m_tree->currentItem(); });

    load(Favorites::instance()->items(), nullptr);
    m_tree->expandAll();
    resize(560, 400);
}

void FavoritesDialog::load(const QList<Favorite> &list, QTreeWidgetItem *parent)
{
    for (const Favorite &f : list) {
        auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
        if (f.separator) {
            item->setText(0, QStringLiteral("――――"));
            item->setData(0, kKindRole, 2);
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsDragEnabled);
        } else if (f.isGroup()) {
            item->setText(0, f.name);
            item->setData(0, kKindRole, 1);
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder-favorites")));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
            load(f.items, item);
        } else {
            item->setText(0, f.name);
            item->setText(1, f.path);
            item->setData(0, kPathRole, f.path);
            item->setData(0, kKindRole, 0);
            item->setIcon(0, QIcon::fromTheme(QStringLiteral("folder")));
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsDragEnabled);
        }
    }
}

QList<Favorite> FavoritesDialog::collect(QTreeWidgetItem *parent) const
{
    QList<Favorite> out;
    const int n = parent ? parent->childCount() : m_tree->topLevelItemCount();
    for (int i = 0; i < n; ++i) {
        QTreeWidgetItem *item = parent ? parent->child(i) : m_tree->topLevelItem(i);
        Favorite f;
        switch (item->data(0, kKindRole).toInt()) {
        case 2:
            f.separator = true;
            break;
        case 1:
            f.name = item->text(0).trimmed();
            f.items = collect(item);
            break;
        default:
            f.name = item->text(0).trimmed();
            f.path = item->data(0, kPathRole).toString();
            break;
        }
        if (f.separator || !f.name.isEmpty())
            out << f;
    }
    return out;
}

QList<Favorite> FavoritesDialog::favorites() const
{
    return collect(nullptr);
}

void FavoritesDialog::accept()
{
    Favorites::instance()->setItems(favorites());
    QDialog::accept();
}
