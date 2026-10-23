#pragma once

#include "core/Apps.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QListWidget;

// Choose an application for files: those that say they open the type first,
// then every other one; search by name. "Always use" makes it Linopus's
// default for the type.
class OpenWithDialog : public QDialog
{
    Q_OBJECT
public:
    OpenWithDialog(const QStringList &files, QWidget *parent = nullptr);

    AppInfo chosen() const;
    bool alwaysUse() const;
    QString mimeType() const { return m_mime; }
    QListWidget *list() const { return m_list; }
    QLineEdit *search() const { return m_search; }
    QCheckBox *alwaysBox() const { return m_always; }

    static QIcon iconFor(const AppInfo &app);

private:
    void fill();

    QStringList m_files;
    QString m_mime;
    QLineEdit *m_search;
    QListWidget *m_list;
    QCheckBox *m_always;
};
