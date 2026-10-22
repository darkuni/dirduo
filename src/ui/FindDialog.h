#pragma once

#include "core/FindEngine.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QTimer>

#include <memory>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

// Find files by name, text inside, size and date, under one or more folders.
// The results go into a collection (Find Results unless named otherwise),
// which the lister opens like a folder: every command works on them.
class FindDialog : public QDialog
{
    Q_OBJECT
public:
    FindDialog(const QStringList &roots, const FindCriteria &criteria, const QString &collection, QWidget *parent = nullptr);
    ~FindDialog() override;

    FindCriteria criteria() const;
    void setCriteria(const FindCriteria &c);
    QStringList roots() const;
    QString collection() const;

    void startSearch();
    bool isSearching() const { return m_busy; }
    QStringList results() const { return m_results; }

    // For tests.
    QLineEdit *nameEdit() const { return m_name; }
    QLineEdit *containsEdit() const { return m_contains; }
    QString status() const;

Q_SIGNALS:
    void resultsReady(const QString &collectionLocation, int count);

private:
    void refreshPresets(const QString &select = {});

    std::shared_ptr<ScanProgress> m_progress;
    QFutureWatcher<QStringList> m_watcher;
    QTimer m_tick;
    bool m_busy = false;
    bool m_truncated = false;
    QStringList m_results;

    QComboBox *m_presets;
    QLineEdit *m_name;
    QCheckBox *m_regex;
    QCheckBox *m_case;
    QLineEdit *m_contains;
    QComboBox *m_type;
    QLineEdit *m_minSize;
    QLineEdit *m_maxSize;
    QComboBox *m_newer;
    QLineEdit *m_older;
    QListWidget *m_roots;
    QCheckBox *m_recurse;
    QCheckBox *m_hidden;
    QLineEdit *m_collection;
    QPushButton *m_search;
    QLabel *m_status;
};
