#pragma once

#include "core/Attributes.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QStringList>

#include <atomic>
#include <memory>

class QCheckBox;
class QDateTimeEdit;
class QLabel;

// Properties of the selection: where it is, its size (folders are measured
// in the background), link target, dates and permissions. Dates and
// permissions can be changed, for several items at once and optionally for
// everything inside folders.
class PropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PropertiesDialog(const QStringList &paths, QWidget *parent = nullptr);
    ~PropertiesDialog() override;

    // What OK would change (empty when nothing was touched).
    Attributes::Change change() const;
    // Applies change() to the items; false with problems listed when some failed.
    bool applyChanges(QStringList *problems);

    QCheckBox *permissionBox(int who, int what) const { return m_perm[who][what]; } // who: 0 owner..2 others; what: 0 r, 1 w, 2 x
    QDateTimeEdit *modifiedEdit() const { return m_modified; }
    QCheckBox *recurseBox() const { return m_recurse; }
    QString sizeText() const;
    QString linkText() const;
    bool measuring() const { return m_watcher.isRunning(); }

    void accept() override;

Q_SIGNALS:
    void changed(); // something was written (the listings refresh)

private:
    struct Totals
    {
        qint64 bytes = 0;
        int files = 0;
        int folders = 0;
    };

    QStringList m_paths;
    QList<Attributes::Info> m_infos;
    QLabel *m_size = nullptr;
    QLabel *m_link = nullptr;
    QDateTimeEdit *m_modified = nullptr;
    QDateTimeEdit *m_accessed = nullptr;
    bool m_modifiedTouched = false;
    bool m_accessedTouched = false;
    QCheckBox *m_perm[3][3] = {};
    Qt::CheckState m_initial[3][3] = {};
    QCheckBox *m_recurse = nullptr;
    QFutureWatcher<Totals> m_watcher;
    std::shared_ptr<std::atomic<bool>> m_stop;
};
