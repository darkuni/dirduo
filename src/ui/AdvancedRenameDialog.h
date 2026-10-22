#pragma once

#include "core/RenameEngine.h"

#include <QDialog>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTreeWidget;

// Rename many items at once: wildcard, find and replace or regular
// expression, numbering, dates, case, with a live before and after preview
// and saved presets. Nothing is renamed until Rename is pressed, and nothing
// is renamed at all while any new name clashes.
class AdvancedRenameDialog : public QDialog
{
    Q_OBJECT
public:
    AdvancedRenameDialog(const QStringList &paths, const RenameOptions &initial, QWidget *parent = nullptr);

    RenameOptions options() const;
    void setOptions(const RenameOptions &o);
    QList<RenameItem> items() const { return m_items; }
    // (new path, old path) of what was renamed, after accept.
    QList<QPair<QString, QString>> done() const { return m_done; }

    // For tests.
    QLineEdit *oldEdit() const { return m_old; }
    QLineEdit *newEdit() const { return m_new; }
    QComboBox *modeBox() const { return m_mode; }
    QTreeWidget *previewTree() const { return m_preview; }
    QPushButton *renameButton() const;
    void refreshNow();

    void accept() override;

private:
    void schedule();
    void refreshPresets(const QString &select = {});

    QStringList m_paths;
    QList<RenameItem> m_items;
    QList<QPair<QString, QString>> m_done;
    QTimer m_debounce;
    bool m_loading = false;

    QComboBox *m_presets;
    QComboBox *m_mode;
    QLineEdit *m_old;
    QLineEdit *m_new;
    QLabel *m_oldLabel;
    QLabel *m_newLabel;
    QCheckBox *m_ignoreExt;
    QCheckBox *m_caseSensitive;
    QCheckBox *m_trim;
    QComboBox *m_case;
    QComboBox *m_extCase;
    QSpinBox *m_start;
    QSpinBox *m_step;
    QSpinBox *m_width;
    QTreeWidget *m_preview;
    QLabel *m_summary;
    QDialogButtonBox *m_buttons;
};
