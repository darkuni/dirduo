#pragma once

#include "core/Favorites.h"

#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;

// Organize Favorites: rename by editing, drag to reorder or into a group,
// add groups and separators, remove.
class FavoritesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FavoritesDialog(QWidget *parent = nullptr);

    QList<Favorite> favorites() const;
    QTreeWidget *tree() const { return m_tree; }

    void accept() override;

private:
    void load(const QList<Favorite> &list, QTreeWidgetItem *parent);
    QList<Favorite> collect(QTreeWidgetItem *parent) const;

    QTreeWidget *m_tree;
};
