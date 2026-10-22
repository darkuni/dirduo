#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;

// Edit > Select...: pick files by name, kind, size, age and how they compare
// with the other display. Builds a Select command, so everything here can go
// on a button too (the command shows at the bottom).
class SelectDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SelectDialog(QWidget *parent = nullptr);

    // The command for the current settings; mode is "", "ADD" or "DESELECT".
    QString command(const QString &mode = {}) const;

    QLineEdit *patternEdit() const { return m_pattern; }
    QComboBox *kindBox() const { return m_kind; }
    QComboBox *compareBox() const { return m_compare; }
    QCheckBox *bothBox() const { return m_both; }

Q_SIGNALS:
    void commandRequested(const QString &command);

private:
    void updatePreview();

    QLineEdit *m_pattern;
    QComboBox *m_kind;
    QComboBox *m_type;
    QLineEdit *m_minSize;
    QLineEdit *m_maxSize;
    QLineEdit *m_newer;
    QLineEdit *m_older;
    QComboBox *m_compare;
    QCheckBox *m_both;
    QLineEdit *m_preview;
};
