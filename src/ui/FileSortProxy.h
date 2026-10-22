#pragma once

#include <QSortFilterProxyModel>

// Folders first in either sort order, then natural ordering ("audio2" before
// "audio10"). Hides dotfiles unless asked.
class FileSortProxy : public QSortFilterProxyModel
{
    Q_OBJECT
public:
    explicit FileSortProxy(QObject *parent = nullptr);

    void setShowHidden(bool show);
    bool showHidden() const { return m_showHidden; }

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool nameLess(const QModelIndex &left, const QModelIndex &right) const;

    bool m_showHidden = false;
};
