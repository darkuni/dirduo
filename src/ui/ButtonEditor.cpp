#include "ui/ButtonEditor.h"

#include "core/CommandCatalog.h"
#include "core/Condition.h"
#include "core/ScriptRunner.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QPlainTextEdit *commandEdit(QWidget *parent, const QString &text, const QString &placeholder)
{
    auto *e = new QPlainTextEdit(text, parent);
    e->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    e->setPlaceholderText(placeholder);
    e->setTabChangesFocus(true);
    const int line = e->fontMetrics().lineSpacing();
    e->setFixedHeight(line * 3 + 12);
    // A shell script gets room to breathe.
    QObject::connect(e, &QPlainTextEdit::textChanged, e, [e, line]() {
        e->setFixedHeight(ScriptRunner::isScript(e->toPlainText()) ? line * 14 + 12 : line * 3 + 12);
    });
    e->setFixedHeight(ScriptRunner::isScript(text) ? line * 14 + 12 : line * 3 + 12);
    return e;
}

QString conditionHelp()
{
    QStringList lines{QStringLiteral("Combine with && (and), || (or), ! (not) and parentheses.")};
    for (const ConditionInfo &c : CommandCatalog::conditions()) {
        QString l = c.name;
        if (!c.values.isEmpty())
            l += QLatin1Char('=') + c.values.join(QLatin1Char('|'));
        lines << QStringLiteral("%1 - %2").arg(l, c.description);
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace

QStringList ButtonEditor::iconNames()
{
    return {
        QStringLiteral("edit-copy"), QStringLiteral("edit-cut"), QStringLiteral("edit-paste"), QStringLiteral("edit-delete"),
        QStringLiteral("edit-rename"), QStringLiteral("edit-find"), QStringLiteral("edit-select-all"), QStringLiteral("edit-undo"),
        QStringLiteral("transform-move"), QStringLiteral("folder-new"), QStringLiteral("document-new"), QStringLiteral("document-open"),
        QStringLiteral("document-save"), QStringLiteral("document-edit"), QStringLiteral("document-export"), QStringLiteral("document-import"),
        QStringLiteral("go-up"), QStringLiteral("go-previous"), QStringLiteral("go-next"), QStringLiteral("go-home"),
        QStringLiteral("go-jump"), QStringLiteral("view-refresh"), QStringLiteral("view-split-left-right"), QStringLiteral("view-split-top-bottom"),
        QStringLiteral("view-list-details"), QStringLiteral("view-list-icons"), QStringLiteral("view-list-tree"), QStringLiteral("view-filter"),
        QStringLiteral("view-hidden"), QStringLiteral("view-group"), QStringLiteral("view-preview"), QStringLiteral("object-locked"),
        QStringLiteral("folder"), QStringLiteral("folder-pictures"), QStringLiteral("folder-music"), QStringLiteral("folder-videos"),
        QStringLiteral("folder-download"), QStringLiteral("folder-documents"), QStringLiteral("folder-remote"), QStringLiteral("folder-games"),
        QStringLiteral("drive-harddisk"), QStringLiteral("media-flash-sd-mmc"), QStringLiteral("drive-removable-media"), QStringLiteral("network-server"),
        QStringLiteral("archive-insert"), QStringLiteral("archive-extract"), QStringLiteral("package-x-generic"), QStringLiteral("utilities-terminal"),
        QStringLiteral("system-run"), QStringLiteral("applications-games"), QStringLiteral("input-gaming"), QStringLiteral("image-x-generic"),
        QStringLiteral("audio-x-generic"), QStringLiteral("video-x-generic"), QStringLiteral("text-x-generic"), QStringLiteral("media-optical"),
        QStringLiteral("color-management"), QStringLiteral("configure"), QStringLiteral("bookmarks"), QStringLiteral("starred"),
        QStringLiteral("emblem-favorite"), QStringLiteral("tag"), QStringLiteral("user-trash"), QStringLiteral("dialog-information"),
    };
}

ButtonEditor::ButtonEditor(const ButtonDef &def, QWidget *parent)
    : QDialog(parent)
    , m_children(def.children)
{
    setWindowTitle(QStringLiteral("Edit Button"));
    auto *layout = new QVBoxLayout(this);

    auto *look = new QFormLayout;
    m_label = new QLineEdit(def.label, this);
    look->addRow(QStringLiteral("Label:"), m_label);
    auto *iconRow = new QHBoxLayout;
    m_icon = new QComboBox(this);
    m_icon->setEditable(true);
    m_icon->addItem(QString());
    for (const QString &name : iconNames())
        m_icon->addItem(QIcon::fromTheme(name), name);
    m_icon->setCurrentText(def.icon);
    m_icon->setToolTip(QStringLiteral("Any icon name from the icon theme"));
    m_iconPreview = new QLabel(this);
    m_iconPreview->setFixedSize(24, 24);
    iconRow->addWidget(m_icon, 1);
    iconRow->addWidget(m_iconPreview);
    look->addRow(QStringLiteral("Icon:"), iconRow);
    m_tip = new QLineEdit(def.tip, this);
    look->addRow(QStringLiteral("Tooltip:"), m_tip);
    layout->addLayout(look);

    m_isMenu = new QCheckBox(QStringLiteral("Menu: opens a list of buttons instead of running a command"), this);
    m_isMenu->setChecked(def.isMenu());
    layout->addWidget(m_isMenu);

    m_commands = new QGroupBox(QStringLiteral("Commands (one per line; {filepath} and the other codes work)"), this);
    auto *cl = new QFormLayout(m_commands);
    m_left = commandEdit(m_commands, def.left, QStringLiteral("Copy"));
    m_right = commandEdit(m_commands, def.right, QStringLiteral("Optional: Copy AS"));
    m_middle = commandEdit(m_commands, def.middle, QStringLiteral("Optional"));
    cl->addRow(QStringLiteral("Left-click:"), m_left);
    cl->addRow(QStringLiteral("Right-click:"), m_right);
    cl->addRow(QStringLiteral("Middle-click:"), m_middle);
    // Shell scripts: a first line of #!/bin/bash (or any #!) makes it one.
    auto *scriptRow = new QHBoxLayout;
    auto *scriptHelp = new QLabel(QStringLiteral("Start with <b>#!/bin/bash</b> to run a script on the system: the selected items are "
                                                 "\"$@\", it starts in the source folder, and $LINOPUS_SOURCE, $LINOPUS_DEST and "
                                                 "$LINOPUS_FOCUSED are set. Add <b># linopus: terminal</b> to run it in Konsole, "
                                                 "<b># linopus: output</b> to always see what it printed."),
                                  m_commands);
    scriptHelp->setWordWrap(true);
    auto *newScript = new QPushButton(QStringLiteral("New Script"), m_commands);
    newScript->setToolTip(QStringLiteral("Put a bash script template in Left-click"));
    connect(newScript, &QPushButton::clicked, this, [this]() {
        m_left->setPlainText(QStringLiteral("#!/bin/bash\n# linopus: output\n# Selected items: \"$@\"   Source: \"$LINOPUS_SOURCE\"   Destination: \"$LINOPUS_DEST\"\n"
                                            "for f in \"$@\"; do\n    echo \"$f\"\ndone\n"));
        m_left->setFocus();
    });
    scriptRow->addWidget(scriptHelp, 1);
    scriptRow->addWidget(newScript, 0, Qt::AlignTop);
    cl->addRow(scriptRow);
    layout->addWidget(m_commands);

    m_menuPage = new QGroupBox(QStringLiteral("Menu items"), this);
    auto *menuOuter = new QVBoxLayout(m_menuPage);
    auto *dynRow = new QHBoxLayout;
    dynRow->addWidget(new QLabel(QStringLiteral("Fill with:"), m_menuPage));
    m_dynamic = new QComboBox(m_menuPage);
    m_dynamic->addItem(QStringLiteral("The items below"), QString());
    m_dynamic->addItem(QStringLiteral("Favorites (kept up to date)"), QStringLiteral("favorites"));
    m_dynamic->addItem(QStringLiteral("Frequent folders"), QStringLiteral("frequent"));
    m_dynamic->addItem(QStringLiteral("Aliases"), QStringLiteral("aliases"));
    m_dynamic->addItem(QStringLiteral("Collections"), QStringLiteral("collections"));
    m_dynamic->addItem(QStringLiteral("Network and servers"), QStringLiteral("servers"));
    m_dynamic->setCurrentIndex(qMax(0, m_dynamic->findData(def.dynamic)));
    dynRow->addWidget(m_dynamic, 1);
    menuOuter->addLayout(dynRow);
    auto *ml = new QHBoxLayout;
    menuOuter->addLayout(ml);
    m_childList = new QListWidget(m_menuPage);
    ml->addWidget(m_childList, 1);
    auto *mb = new QVBoxLayout;
    auto addButton = [&](const QString &text) {
        auto *b = new QPushButton(text, m_menuPage);
        mb->addWidget(b);
        return b;
    };
    QPushButton *add = addButton(QStringLiteral("Add..."));
    QPushButton *addSep = addButton(QStringLiteral("Add Separator"));
    QPushButton *edit = addButton(QStringLiteral("Edit..."));
    QPushButton *remove = addButton(QStringLiteral("Remove"));
    QPushButton *up = addButton(QStringLiteral("Up"));
    QPushButton *down = addButton(QStringLiteral("Down"));
    mb->addStretch(1);
    ml->addLayout(mb);
    layout->addWidget(m_menuPage);

    auto row = [this]() { return m_childList->currentRow(); };
    connect(add, &QPushButton::clicked, this, [this, row]() {
        ButtonDef d;
        d.label = QStringLiteral("New Item");
        ButtonEditor e(d, this);
        if (e.exec() == QDialog::Accepted) {
            m_children.insert(row() + 1, e.button());
            refreshChildren();
            m_childList->setCurrentRow(row() + 1);
        }
    });
    connect(addSep, &QPushButton::clicked, this, [this, row]() {
        m_children.insert(row() + 1, ButtonDef::makeSeparator());
        refreshChildren();
    });
    auto editChild = [this, row]() {
        const int r = row();
        if (r < 0 || m_children.at(r).separator)
            return;
        ButtonEditor e(m_children.at(r), this);
        if (e.exec() == QDialog::Accepted) {
            m_children[r] = e.button();
            refreshChildren();
            m_childList->setCurrentRow(r);
        }
    };
    connect(edit, &QPushButton::clicked, this, editChild);
    connect(m_childList, &QListWidget::itemDoubleClicked, this, editChild);
    connect(remove, &QPushButton::clicked, this, [this, row]() {
        if (row() >= 0) {
            m_children.removeAt(row());
            refreshChildren();
        }
    });
    auto move = [this, row](int delta) {
        const int r = row();
        const int to = r + delta;
        if (r < 0 || to < 0 || to >= m_children.size())
            return;
        m_children.swapItemsAt(r, to);
        refreshChildren();
        m_childList->setCurrentRow(to);
    };
    connect(up, &QPushButton::clicked, this, [move]() { move(-1); });
    connect(down, &QPushButton::clicked, this, [move]() { move(1); });

    auto *when = new QGroupBox(QStringLiteral("State"), this);
    auto *wl = new QFormLayout(when);
    m_enable = new QLineEdit(def.enableWhen, when);
    m_enable->setPlaceholderText(QStringLiteral("Always enabled. Example: selection && dest"));
    m_enable->setToolTip(conditionHelp());
    m_check = new QLineEdit(def.checkWhen, when);
    m_check->setPlaceholderText(QStringLiteral("Never pressed. Example: dual"));
    m_check->setToolTip(conditionHelp());
    m_key = new QKeySequenceEdit(QKeySequence(def.key, QKeySequence::PortableText), when);
    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(m_key, 1);
    auto *clearKey = new QToolButton(when);
    clearKey->setText(QStringLiteral("Clear"));
    connect(clearKey, &QToolButton::clicked, m_key, &QKeySequenceEdit::clear);
    keyRow->addWidget(clearKey);
    wl->addRow(QStringLiteral("Enabled when:"), m_enable);
    wl->addRow(QStringLiteral("Pressed when:"), m_check);
    wl->addRow(QStringLiteral("Hotkey:"), keyRow);
    auto *hint = new QLabel(QStringLiteral("Hover the State fields for the words they understand."), when);
    hint->setEnabled(false);
    wl->addRow(hint);
    layout->addWidget(when);

    m_error = new QLabel(this);
    m_error->setStyleSheet(QStringLiteral("color: #c03030;"));
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ButtonEditor::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ButtonEditor::reject);
    layout->addWidget(buttons);

    connect(m_isMenu, &QCheckBox::toggled, this, &ButtonEditor::updateMode);
    connect(m_dynamic, &QComboBox::currentIndexChanged, this, [this]() {
        const bool fixed = m_dynamic->currentData().toString().isEmpty();
        m_childList->setEnabled(fixed);
        for (QPushButton *b : m_menuPage->findChildren<QPushButton *>())
            b->setEnabled(fixed);
    });
    Q_EMIT m_dynamic->currentIndexChanged(m_dynamic->currentIndex());
    connect(m_icon, &QComboBox::currentTextChanged, this, &ButtonEditor::updatePreview);
    refreshChildren();
    updateMode();
    updatePreview();
    resize(560, sizeHint().height());
}

void ButtonEditor::updateMode()
{
    m_commands->setVisible(!m_isMenu->isChecked());
    m_menuPage->setVisible(m_isMenu->isChecked());
    adjustSize();
}

void ButtonEditor::updatePreview()
{
    const QIcon i = QIcon::fromTheme(m_icon->currentText().trimmed());
    m_iconPreview->setPixmap(i.isNull() ? QPixmap() : i.pixmap(22, 22));
}

void ButtonEditor::refreshChildren()
{
    m_childList->clear();
    for (const ButtonDef &c : std::as_const(m_children)) {
        if (c.separator) {
            m_childList->addItem(QStringLiteral("――――――――"));
            continue;
        }
        auto *item = new QListWidgetItem(QIcon::fromTheme(c.icon), c.isMenu() ? c.label + QStringLiteral("  ▸") : c.label);
        item->setToolTip(c.isMenu() ? QStringLiteral("Submenu") : c.left);
        m_childList->addItem(item);
    }
}

ButtonDef ButtonEditor::button() const
{
    ButtonDef d;
    d.label = m_label->text().trimmed();
    d.icon = m_icon->currentText().trimmed();
    d.tip = m_tip->text().trimmed();
    if (m_isMenu->isChecked()) {
        d.dynamic = m_dynamic->currentData().toString();
        if (d.dynamic.isEmpty())
            d.children = m_children;
    } else {
        d.left = m_left->toPlainText().trimmed();
        d.right = m_right->toPlainText().trimmed();
        d.middle = m_middle->toPlainText().trimmed();
    }
    d.enableWhen = m_enable->text().trimmed();
    d.checkWhen = m_check->text().trimmed();
    d.key = m_key->keySequence().toString(QKeySequence::PortableText);
    return d;
}

QString ButtonEditor::validationError() const
{
    const ButtonDef d = button();
    if (d.label.isEmpty())
        return QStringLiteral("The button needs a label.");
    if (m_isMenu->isChecked() && d.children.isEmpty() && d.dynamic.isEmpty())
        return QStringLiteral("A menu needs at least one item.");
    if (!m_isMenu->isChecked() && d.left.isEmpty())
        return QStringLiteral("Left-click needs a command.");
    if (!d.left.isEmpty() && !ScriptRunner::isScript(d.left)) {
        const QString first = d.left.section(QLatin1Char('\n'), 0, 0).trimmed().section(QLatin1Char(' '), 0, 0);
        if (!CommandCatalog::find(first))
            return QStringLiteral("Left-click: there is no command called \"%1\".").arg(first);
    }
    const QStringList known = CommandCatalog::conditionNames();
    QString err;
    if (!Condition::check(d.enableWhen, known, &err))
        return QStringLiteral("Enabled when: %1").arg(err);
    if (!Condition::check(d.checkWhen, known, &err))
        return QStringLiteral("Pressed when: %1").arg(err);
    return {};
}

void ButtonEditor::accept()
{
    const QString err = validationError();
    if (!err.isEmpty()) {
        m_error->setText(err);
        m_error->show();
        return;
    }
    QDialog::accept();
}
