#pragma once

#include "core/CommandParser.h"

#include <QDialog>
#include <QStringList>

class QComboBox;
class QLabel;
class QLineEdit;

// Add to Archive: the archive's name, format and folder. Accepting it gives
// an "Archive CREATE ..." command to run; the format is remembered.
class ArchiveDialog : public QDialog
{
    Q_OBJECT
public:
    // destFolder: the destination display's folder, offered as a place to save
    // it (empty when there is none).
    ArchiveDialog(const QStringList &sources, const QString &sourceFolder, const QString &destFolder,
                  QWidget *parent = nullptr);

    QString command() const;          // as typed, without the password
    ParsedCommand parsedCommand() const; // what to run, with it
    QLineEdit *nameEdit() const { return m_name; }
    QComboBox *formatBox() const { return m_format; }
    QComboBox *whereBox() const { return m_where; }
    QComboBox *existsBox() const { return m_exists; }
    QLineEdit *passwordEdit() const { return m_password; }

    static QString savedFormat(); // "zip" unless another was used last

    void accept() override;

private:
    void formatChanged();

    QStringList m_sources;
    QString m_sourceFolder;
    QString m_destFolder;
    QLineEdit *m_name = nullptr;
    QComboBox *m_format = nullptr;
    QComboBox *m_where = nullptr;
    QComboBox *m_exists = nullptr;
    QLineEdit *m_password = nullptr;
    QLabel *m_what = nullptr;
    QString m_lastExt;
};
