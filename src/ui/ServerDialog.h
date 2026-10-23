#pragma once

#include "core/Servers.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSpinBox;

// Add or edit one server: name, kind, host, port, user and folder.
class ServerDialog : public QDialog
{
    Q_OBJECT
public:
    // Starts from url when given (the folder being shown, say).
    explicit ServerDialog(const Server &start, QWidget *parent = nullptr);

    Server server() const;
    QLineEdit *nameEdit() const { return m_name; }
    QComboBox *kindBox() const { return m_kind; }
    QLineEdit *hostEdit() const { return m_host; }
    QLineEdit *userEdit() const { return m_user; }
    QLineEdit *folderEdit() const { return m_folder; }
    QSpinBox *portBox() const { return m_port; }

    void accept() override;

private:
    void updatePreview();

    QLineEdit *m_name;
    QComboBox *m_kind;
    QLineEdit *m_host;
    QSpinBox *m_port;
    QLineEdit *m_user;
    QLineEdit *m_folder;
    QLabel *m_preview;
    bool m_nameTouched = false;
};

// The address book: the list, with Add, Edit, Remove and Open.
class ServersDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ServersDialog(QWidget *parent = nullptr);
    QListWidget *list() const { return m_list; }

Q_SIGNALS:
    void openRequested(const QString &url);

private:
    void reload();

    QListWidget *m_list;
};
