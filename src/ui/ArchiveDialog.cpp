#include "ui/ArchiveDialog.h"

#include "core/Archive.h"
#include "core/CommandEngine.h"
#include "core/FileOps.h"
#include "core/Location.h"
#include "core/Paths.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace {

QSettings settings()
{
    return QSettings(Paths::configFile(QStringLiteral("dirduo.ini")), QSettings::IniFormat);
}

QString quoted(const QString &s)
{
    QString v = s;
    v.replace(QLatin1Char('"'), QStringLiteral("\\\""));
    return QLatin1Char('"') + v + QLatin1Char('"');
}

} // namespace

QString ArchiveDialog::savedFormat()
{
    const QString key = settings().value(QStringLiteral("Archive/format"), QStringLiteral("zip")).toString();
    Archive::Format f;
    return Archive::formatFromKey(key, f) ? key : QStringLiteral("zip");
}

ArchiveDialog::ArchiveDialog(const QStringList &sources, const QString &sourceFolder, const QString &destFolder,
                             QWidget *parent)
    : QDialog(parent)
    , m_sources(sources)
    , m_sourceFolder(sourceFolder)
    , m_destFolder(destFolder)
{
    setWindowTitle(QStringLiteral("Add to Archive"));
    auto *layout = new QVBoxLayout(this);
    m_what = new QLabel(sources.size() == 1 ? QStringLiteral("Put \"%1\" in a new archive.").arg(Location::fileName(sources.first()))
                                            : QStringLiteral("Put %1 items in a new archive.").arg(sources.size()),
                        this);
    m_what->setWordWrap(true);
    layout->addWidget(m_what);

    auto *form = new QFormLayout;
    const QString key = savedFormat();
    m_format = new QComboBox(this);
    for (Archive::Format f : Archive::formats()) {
        const QString ext = Archive::extensionFor(f);
        const QString label = Archive::displayName(f) == ext ? QStringLiteral(".%1").arg(ext)
                                                            : QStringLiteral("%1 (.%2)").arg(Archive::displayName(f), ext);
        m_format->addItem(label, ext);
    }
    m_format->setCurrentIndex(qMax(0, m_format->findData(key)));
    m_lastExt = m_format->currentData().toString();

    m_name = new QLineEdit(CommandEngine::suggestedArchiveName(sources, sourceFolder, m_lastExt), this);
    form->addRow(QStringLiteral("&Name:"), m_name);
    form->addRow(QStringLiteral("&Format:"), m_format);

    m_where = new QComboBox(this);
    m_where->addItem(QStringLiteral("Here (%1)").arg(Location::display(sourceFolder)), QStringLiteral("here"));
    if (!destFolder.isEmpty() && destFolder != sourceFolder)
        m_where->addItem(QStringLiteral("Destination (%1)").arg(Location::display(destFolder)), QStringLiteral("dest"));
    form->addRow(QStringLiteral("&Save in:"), m_where);

    m_exists = new QComboBox(this);
    m_exists->addItem(QStringLiteral("Give the new one another name"), QStringLiteral("rename"));
    m_exists->addItem(QStringLiteral("Replace it"), QStringLiteral("replace"));
    form->addRow(QStringLiteral("If the name is &taken:"), m_exists);

    m_password = new QLineEdit(this);
    m_password->setEchoMode(QLineEdit::Password);
    m_password->setClearButtonEnabled(true);
    form->addRow(QStringLiteral("&Password:"), m_password);
    layout->addLayout(form);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Create"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_format, &QComboBox::currentIndexChanged, this, &ArchiveDialog::formatChanged);
    formatChanged();
    // Select the name without its extension, ready to type over.
    m_name->setSelection(0, m_name->text().size() - m_lastExt.size() - 1);
    setMinimumWidth(fontMetrics().averageCharWidth() * 60);
}

void ArchiveDialog::formatChanged()
{
    // Swap the extension, keep the rest of what was typed.
    const QString ext = m_format->currentData().toString();
    QString name = m_name->text().trimmed();
    Archive::Format had;
    if (Archive::formatFromName(name, had))
        name.chop(Archive::extensionFor(had).size() + 1);
    else if (name.endsWith(QLatin1Char('.') + m_lastExt, Qt::CaseInsensitive))
        name.chop(m_lastExt.size() + 1);
    m_name->setText(name + QLatin1Char('.') + ext);
    m_lastExt = ext;
    // Only zip archives are encrypted here.
    Archive::Format f;
    const bool can = Archive::formatFromKey(ext, f) && Archive::canEncrypt(f);
    m_password->setEnabled(can);
    m_password->setPlaceholderText(can ? QStringLiteral("None (optional, AES-256)") : QStringLiteral("Zip archives only"));
}

QString ArchiveDialog::command() const
{
    QString c = QStringLiteral("Archive CREATE NAME=%1 FORMAT=%2 WHENEXISTS=%3")
                    .arg(quoted(m_name->text().trimmed()), m_format->currentData().toString(), m_exists->currentData().toString());
    if (m_where->currentData().toString() == QLatin1String("dest"))
        c += QStringLiteral(" DEST");
    return c;
}

ParsedCommand ArchiveDialog::parsedCommand() const
{
    // Built directly, not typed out: a password may hold quotes or braces.
    ParsedCommand c;
    c.name = QStringLiteral("Archive");
    c.positional << QStringLiteral("CREATE");
    if (m_where->currentData().toString() == QLatin1String("dest"))
        c.positional << QStringLiteral("DEST");
    c.args.insert(QStringLiteral("NAME"), {m_name->text().trimmed()});
    c.args.insert(QStringLiteral("FORMAT"), {m_format->currentData().toString()});
    c.args.insert(QStringLiteral("WHENEXISTS"), {m_exists->currentData().toString()});
    if (m_password->isEnabled() && !m_password->text().isEmpty())
        c.args.insert(QStringLiteral("PASSWORD"), {m_password->text()});
    c.raw = command();
    return c;
}

void ArchiveDialog::accept()
{
    QString why;
    if (!FileOps::isValidFileName(m_name->text().trimmed(), &why)) {
        QMessageBox::warning(this, windowTitle(), why);
        m_name->setFocus();
        return;
    }
    QSettings s = settings();
    s.setValue(QStringLiteral("Archive/format"), m_format->currentData().toString());
    QDialog::accept();
}
