#include "ui/ServerDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>
#include <QVBoxLayout>

ServerDialog::ServerDialog(const Server &start, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(start.name.isEmpty() ? QStringLiteral("Add Server") : QStringLiteral("Edit Server"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;
    m_name = new QLineEdit(start.name, this);
    m_kind = new QComboBox(this);
    m_kind->addItem(QStringLiteral("SFTP (SSH)"), QStringLiteral("sftp"));
    m_kind->addItem(QStringLiteral("Windows share (SMB)"), QStringLiteral("smb"));
    m_kind->addItem(QStringLiteral("FTP"), QStringLiteral("ftp"));
    m_kind->addItem(QStringLiteral("WebDAV"), QStringLiteral("webdav"));
    m_kind->addItem(QStringLiteral("WebDAV over HTTPS"), QStringLiteral("webdavs"));
    m_host = new QLineEdit(this);
    m_host->setPlaceholderText(QStringLiteral("nas, 192.168.1.20, myserver.example.com"));
    m_port = new QSpinBox(this);
    m_port->setRange(0, 65535);
    m_port->setSpecialValueText(QStringLiteral("Default"));
    m_user = new QLineEdit(this);
    m_user->setPlaceholderText(QStringLiteral("(asked when needed)"));
    m_folder = new QLineEdit(this);
    m_folder->setPlaceholderText(QStringLiteral("/  or  share/folder"));
    m_preview = new QLabel(this);
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(QStringLiteral("&Name:"), m_name);
    form->addRow(QStringLiteral("&Kind:"), m_kind);
    form->addRow(QStringLiteral("&Server:"), m_host);
    form->addRow(QStringLiteral("&Port:"), m_port);
    form->addRow(QStringLiteral("&User:"), m_user);
    form->addRow(QStringLiteral("&Folder:"), m_folder);
    form->addRow(QStringLiteral("Address:"), m_preview);
    layout->addLayout(form);
    auto *note = new QLabel(QStringLiteral("Passwords are asked for when you connect, and your desktop's wallet can remember them."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    if (!start.url.isEmpty()) {
        const QUrl u(start.url);
        const int k = m_kind->findData(u.scheme().toLower());
        m_kind->setCurrentIndex(qMax(0, k));
        m_host->setText(u.host());
        m_port->setValue(qMax(0, u.port()));
        m_user->setText(u.userName());
        m_folder->setText(u.path() == QLatin1String("/") ? QString() : u.path());
    }
    m_nameTouched = !start.name.isEmpty();
    connect(m_name, &QLineEdit::textEdited, this, [this]() { m_nameTouched = true; });
    for (QLineEdit *e : {m_host, m_user, m_folder})
        connect(e, &QLineEdit::textChanged, this, &ServerDialog::updatePreview);
    connect(m_port, &QSpinBox::valueChanged, this, &ServerDialog::updatePreview);
    connect(m_kind, &QComboBox::currentIndexChanged, this, &ServerDialog::updatePreview);
    updatePreview();
    setMinimumWidth(fontMetrics().averageCharWidth() * 60);
}

Server ServerDialog::server() const
{
    const QString url = Servers::makeUrl(m_kind->currentData().toString(), m_host->text(), m_port->value(), m_user->text(),
                                         m_folder->text());
    QString name = m_name->text().trimmed();
    if (name.isEmpty())
        name = Servers::suggestedName(url);
    return Server{name, url};
}

void ServerDialog::updatePreview()
{
    const Server s = server();
    m_preview->setText(m_host->text().trimmed().isEmpty() ? QStringLiteral("-") : s.url);
    if (!m_nameTouched)
        m_name->setText(m_host->text().trimmed().isEmpty() ? QString() : Servers::suggestedName(s.url));
}

void ServerDialog::accept()
{
    if (m_host->text().trimmed().isEmpty()) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("Type the server's name or address."));
        m_host->setFocus();
        return;
    }
    QString error;
    if (!Servers::isServerUrl(server().url)) {
        QMessageBox::warning(this, windowTitle(), QStringLiteral("That does not make a server address."));
        return;
    }
    QDialog::accept();
}

// ---------------------------------------------------------------------------

ServersDialog::ServersDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Servers"));
    auto *layout = new QHBoxLayout(this);
    m_list = new QListWidget(this);
    layout->addWidget(m_list, 1);
    auto *side = new QVBoxLayout;
    auto *open = new QPushButton(QStringLiteral("&Open"), this);
    auto *add = new QPushButton(QStringLiteral("&Add..."), this);
    auto *edit = new QPushButton(QStringLiteral("&Edit..."), this);
    auto *remove = new QPushButton(QStringLiteral("&Remove"), this);
    auto *close = new QPushButton(QStringLiteral("Close"), this);
    for (QPushButton *b : {open, add, edit, remove})
        side->addWidget(b);
    side->addStretch(1);
    side->addWidget(close);
    layout->addLayout(side);

    auto current = [this]() -> QString {
        QListWidgetItem *it = m_list->currentItem();
        return it ? it->data(Qt::UserRole).toString() : QString();
    };
    connect(open, &QPushButton::clicked, this, [this, current]() {
        const QString name = current();
        if (name.isEmpty())
            return;
        Q_EMIT openRequested(Servers::instance()->urlFor(name));
        accept();
    });
    connect(m_list, &QListWidget::itemDoubleClicked, open, &QPushButton::click);
    connect(add, &QPushButton::clicked, this, [this]() {
        ServerDialog dlg(Server{}, this);
        if (dlg.exec() == QDialog::Accepted) {
            QString error;
            if (!Servers::instance()->add(dlg.server(), &error))
                QMessageBox::warning(this, windowTitle(), error);
            reload();
        }
    });
    connect(edit, &QPushButton::clicked, this, [this, current]() {
        const QString name = current();
        if (name.isEmpty())
            return;
        ServerDialog dlg(Server{name, Servers::instance()->urlFor(name)}, this);
        if (dlg.exec() == QDialog::Accepted) {
            const Server s = dlg.server();
            QList<Server> list = Servers::instance()->items();
            for (Server &e : list) {
                if (e.name == name)
                    e = s;
            }
            Servers::instance()->setItems(list);
            reload();
        }
    });
    connect(remove, &QPushButton::clicked, this, [this, current]() {
        const QString name = current();
        if (!name.isEmpty() && Servers::instance()->remove(name))
            reload();
    });
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    reload();
    resize(fontMetrics().averageCharWidth() * 70, fontMetrics().height() * 18);
}

void ServersDialog::reload()
{
    m_list->clear();
    for (const Server &s : Servers::instance()->items()) {
        auto *it = new QListWidgetItem(QIcon::fromTheme(QStringLiteral("network-server")), QStringLiteral("%1\n%2").arg(s.name, s.url), m_list);
        it->setData(Qt::UserRole, s.name);
    }
    if (m_list->count() > 0)
        m_list->setCurrentRow(0);
}
