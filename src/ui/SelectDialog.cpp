#include "ui/SelectDialog.h"

#include "core/FileKind.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QString quoted(const QString &v)
{
    if (v.contains(QLatin1Char(' ')) || v.contains(QLatin1Char('"')))
        return QLatin1Char('"') + QString(v).replace(QLatin1Char('"'), QStringLiteral("\\\"")) + QLatin1Char('"');
    return v;
}

} // namespace

SelectDialog::SelectDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Select"));
    auto *layout = new QVBoxLayout(this);
    auto *form = new QFormLayout;

    m_pattern = new QLineEdit(this);
    m_pattern->setPlaceholderText(QStringLiteral("Any name; wildcards, ; between several (*.d64;*.adf)"));
    form->addRow(QStringLiteral("Name:"), m_pattern);

    auto *kinds = new QHBoxLayout;
    m_kind = new QComboBox(this);
    m_kind->addItem(QStringLiteral("Any kind"), QString());
    using C = FileKind::Category;
    for (C c : {C::Image, C::Audio, C::Video, C::Archive, C::DiskImage, C::Document, C::Code, C::Executable, C::Folder})
        m_kind->addItem(FileKind::categoryName(c), FileKind::categoryKey(c));
    m_type = new QComboBox(this);
    m_type->addItem(QStringLiteral("Files and folders"), QStringLiteral("both"));
    m_type->addItem(QStringLiteral("Files only"), QStringLiteral("files"));
    m_type->addItem(QStringLiteral("Folders only"), QStringLiteral("folders"));
    kinds->addWidget(m_kind, 1);
    kinds->addWidget(m_type, 1);
    form->addRow(QStringLiteral("Kind:"), kinds);

    auto *sizes = new QHBoxLayout;
    m_minSize = new QLineEdit(this);
    m_minSize->setPlaceholderText(QStringLiteral("at least, like 10M"));
    m_maxSize = new QLineEdit(this);
    m_maxSize->setPlaceholderText(QStringLiteral("at most, like 700K"));
    sizes->addWidget(m_minSize);
    sizes->addWidget(m_maxSize);
    form->addRow(QStringLiteral("Size:"), sizes);

    auto *ages = new QHBoxLayout;
    m_newer = new QLineEdit(this);
    m_newer->setPlaceholderText(QStringLiteral("newer than: days, or 2026-10-01"));
    m_older = new QLineEdit(this);
    m_older->setPlaceholderText(QStringLiteral("older than: days, or a date"));
    ages->addWidget(m_newer);
    ages->addWidget(m_older);
    form->addRow(QStringLiteral("Modified:"), ages);

    m_compare = new QComboBox(this);
    m_compare->addItem(QStringLiteral("Do not compare"), QString());
    m_compare->addItem(QStringLiteral("Newer here, or not there (what an update would copy)"), QStringLiteral("newerormissing"));
    m_compare->addItem(QStringLiteral("Not in the other display"), QStringLiteral("missing"));
    m_compare->addItem(QStringLiteral("Newer here than there"), QStringLiteral("newer"));
    m_compare->addItem(QStringLiteral("Older here than there"), QStringLiteral("older"));
    m_compare->addItem(QStringLiteral("Same date and size in both"), QStringLiteral("same"));
    m_compare->addItem(QStringLiteral("Same date, different size"), QStringLiteral("different"));
    m_both = new QCheckBox(QStringLiteral("Select them in the other display too"), this);
    form->addRow(QStringLiteral("Compare:"), m_compare);
    form->addRow(QString(), m_both);
    layout->addLayout(form);

    m_preview = new QLineEdit(this);
    m_preview->setReadOnly(true);
    m_preview->setToolTip(QStringLiteral("The same as a command, for a button"));
    layout->addWidget(new QLabel(QStringLiteral("As a command:"), this));
    layout->addWidget(m_preview);

    auto *buttons = new QDialogButtonBox(this);
    QPushButton *select = buttons->addButton(QStringLiteral("Select"), QDialogButtonBox::ActionRole);
    QPushButton *add = buttons->addButton(QStringLiteral("Add"), QDialogButtonBox::ActionRole);
    QPushButton *deselect = buttons->addButton(QStringLiteral("Deselect"), QDialogButtonBox::ActionRole);
    buttons->addButton(QDialogButtonBox::Close);
    select->setDefault(true);
    select->setToolTip(QStringLiteral("Select exactly these"));
    add->setToolTip(QStringLiteral("Add these to what is selected"));
    deselect->setToolTip(QStringLiteral("Take these out of what is selected"));
    connect(select, &QPushButton::clicked, this, [this]() { Q_EMIT commandRequested(command()); });
    connect(add, &QPushButton::clicked, this, [this]() { Q_EMIT commandRequested(command(QStringLiteral("ADD"))); });
    connect(deselect, &QPushButton::clicked, this, [this]() { Q_EMIT commandRequested(command(QStringLiteral("DESELECT"))); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    for (QLineEdit *e : {m_pattern, m_minSize, m_maxSize, m_newer, m_older})
        connect(e, &QLineEdit::textChanged, this, &SelectDialog::updatePreview);
    for (QComboBox *b : {m_kind, m_type, m_compare})
        connect(b, &QComboBox::currentIndexChanged, this, &SelectDialog::updatePreview);
    connect(m_compare, &QComboBox::currentIndexChanged, this, [this]() { m_both->setEnabled(!m_compare->currentData().toString().isEmpty()); });
    connect(m_both, &QCheckBox::toggled, this, &SelectDialog::updatePreview);
    m_both->setEnabled(false);
    updatePreview();
    resize(560, sizeHint().height());
}

QString SelectDialog::command(const QString &mode) const
{
    QStringList parts{QStringLiteral("Select")};
    auto add = [&parts](const QString &key, const QString &value) {
        if (!value.trimmed().isEmpty())
            parts << key + QLatin1Char('=') + quoted(value.trimmed());
    };
    add(QStringLiteral("PATTERN"), m_pattern->text());
    add(QStringLiteral("KIND"), m_kind->currentData().toString());
    if (m_type->currentData().toString() != QLatin1String("both"))
        add(QStringLiteral("TYPE"), m_type->currentData().toString());
    add(QStringLiteral("MINSIZE"), m_minSize->text());
    add(QStringLiteral("MAXSIZE"), m_maxSize->text());
    add(QStringLiteral("NEWER"), m_newer->text());
    add(QStringLiteral("OLDER"), m_older->text());
    add(QStringLiteral("COMPARE"), m_compare->currentData().toString());
    if (m_both->isEnabled() && m_both->isChecked())
        parts << QStringLiteral("BOTH");
    if (parts.size() == 1)
        parts << QStringLiteral("PATTERN=*"); // nothing set: everything
    if (!mode.isEmpty())
        parts << mode;
    return parts.join(QLatin1Char(' '));
}

void SelectDialog::updatePreview()
{
    m_preview->setText(command());
}
