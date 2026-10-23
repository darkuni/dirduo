#pragma once

#include <QRegularExpression>
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

    // Filter bar. Text with * or ? is a wildcard; otherwise "name contains".
    enum class FilterScope { Files, Folders, Both };
    void setFilter(const QString &text, bool invert = false, FilterScope scope = FilterScope::Files);
    QString filterText() const { return m_filterText; }
    bool filterInverted() const { return m_filterInvert; }
    FilterScope filterScope() const { return m_filterScope; }

    // Groups stay in their own order whichever way a column is sorted.
    void setGrouping(bool on);
    bool grouping() const { return m_grouping; }

protected:
    bool lessThan(const QModelIndex &left, const QModelIndex &right) const override;
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;

private:
    bool nameLess(const QModelIndex &left, const QModelIndex &right) const;
    // Two items in the same folder, by the sort column.
    bool siblingLess(const QModelIndex &left, const QModelIndex &right) const;

    void refilter();

    bool m_showHidden = false;
    bool m_junkHidden = true;
    QString m_filterText;
    bool m_filterInvert = false;
    FilterScope m_filterScope = FilterScope::Files;
    QRegularExpression m_filterRe;
    bool m_grouping = false;
};
