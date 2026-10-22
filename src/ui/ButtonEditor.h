#pragma once

#include "ui/ButtonBar.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QWidget;

// Edits one button: what it shows, what each mouse button runs, when it is
// greyed out or pressed, its hotkey, or the buttons in its menu.
class ButtonEditor : public QDialog
{
    Q_OBJECT
public:
    explicit ButtonEditor(const ButtonDef &def, QWidget *parent = nullptr);

    ButtonDef button() const;
    // Empty when the fields are valid; otherwise what to fix.
    QString validationError() const;

    // For tests.
    QLineEdit *labelEdit() const { return m_label; }
    QPlainTextEdit *leftEdit() const { return m_left; }
    QLineEdit *enableEdit() const { return m_enable; }
    QCheckBox *menuCheck() const { return m_isMenu; }
    QComboBox *dynamicBox() const { return m_dynamic; }

    static QStringList iconNames();

    void accept() override;

private:
    void updateMode();
    void updatePreview();
    void refreshChildren();

    QLineEdit *m_label;
    QComboBox *m_icon;
    QLabel *m_iconPreview;
    QLineEdit *m_tip;
    QCheckBox *m_isMenu;
    QWidget *m_commands;
    QPlainTextEdit *m_left;
    QPlainTextEdit *m_right;
    QPlainTextEdit *m_middle;
    QWidget *m_menuPage;
    QListWidget *m_childList;
    QComboBox *m_dynamic;
    QLineEdit *m_enable;
    QLineEdit *m_check;
    QKeySequenceEdit *m_key;
    QLabel *m_error;
    QList<ButtonDef> m_children;
};
