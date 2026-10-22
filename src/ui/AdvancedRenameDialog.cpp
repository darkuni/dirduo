#include "ui/AdvancedRenameDialog.h"

#include "ui/Style.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTreeWidget>
#include <QVBoxLayout>

AdvancedRenameDialog::AdvancedRenameDialog(const QStringList &paths, const RenameOptions &initial, QWidget *parent)
    : QDialog(parent)
    , m_paths(paths)
{
    setWindowTitle(QStringLiteral("Advanced Rename (%1 items)").arg(paths.size()));
    auto *layout = new QVBoxLayout(this);

    auto *presetRow = new QHBoxLayout;
    presetRow->addWidget(new QLabel(QStringLiteral("Preset:"), this));
    m_presets = new QComboBox(this);
    presetRow->addWidget(m_presets, 1);
    auto *savePreset = new QPushButton(QStringLiteral("Save..."), this);
    auto *deletePreset = new QPushButton(QStringLiteral("Delete"), this);
    presetRow->addWidget(savePreset);
    presetRow->addWidget(deletePreset);
    layout->addLayout(presetRow);

    auto *form = new QFormLayout;
    m_mode = new QComboBox(this);
    m_mode->addItem(QStringLiteral("Wildcards (* and ?)"));
    m_mode->addItem(QStringLiteral("Find and replace"));
    m_mode->addItem(QStringLiteral("Regular expression"));
    form->addRow(QStringLiteral("Mode:"), m_mode);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_old = new QLineEdit(this);
    m_old->setFont(mono);
    m_new = new QLineEdit(this);
    m_new->setFont(mono);
    m_oldLabel = new QLabel(this);
    m_newLabel = new QLabel(this);
    form->addRow(m_oldLabel, m_old);
    form->addRow(m_newLabel, m_new);
    auto *tokens = new QLabel(QStringLiteral("In the new name: <code>[#]</code> <code>[###]</code> numbers, <code>[date]</code> today, "
                                             "<code>[mdate]</code> <code>[mtime]</code> modified, <code>[name]</code> "
                                             "<code>[ext]</code> <code>[parent]</code>"),
                              this);
    tokens->setWordWrap(true);
    tokens->setEnabled(false);
    form->addRow(QString(), tokens);
    layout->addLayout(form);

    auto *opts = new QGroupBox(QStringLiteral("Options"), this);
    auto *ol = new QGridLayout(opts);
    m_ignoreExt = new QCheckBox(QStringLiteral("Leave extensions alone"), opts);
    m_caseSensitive = new QCheckBox(QStringLiteral("Match case"), opts);
    m_trim = new QCheckBox(QStringLiteral("Tidy spaces"), opts);
    m_case = new QComboBox(opts);
    m_case->addItems({QStringLiteral("Keep case"), QStringLiteral("lower case"), QStringLiteral("UPPER CASE"),
                      QStringLiteral("Title Case"), QStringLiteral("Sentence case")});
    m_extCase = new QComboBox(opts);
    m_extCase->addItems({QStringLiteral("Extension as is"), QStringLiteral("extension lower"), QStringLiteral("EXTENSION UPPER")});
    m_start = new QSpinBox(opts);
    m_start->setRange(-999999, 999999);
    m_step = new QSpinBox(opts);
    m_step->setRange(-1000, 1000);
    m_width = new QSpinBox(opts);
    m_width->setRange(0, 9);
    m_width->setSpecialValueText(QStringLiteral("auto"));
    ol->addWidget(m_ignoreExt, 0, 0);
    ol->addWidget(m_caseSensitive, 0, 1);
    ol->addWidget(m_trim, 0, 2);
    ol->addWidget(m_case, 1, 0);
    ol->addWidget(m_extCase, 1, 1);
    auto *num = new QHBoxLayout;
    num->addWidget(new QLabel(QStringLiteral("Numbers from"), opts));
    num->addWidget(m_start);
    num->addWidget(new QLabel(QStringLiteral("step"), opts));
    num->addWidget(m_step);
    num->addWidget(new QLabel(QStringLiteral("digits"), opts));
    num->addWidget(m_width);
    num->addStretch(1);
    ol->addLayout(num, 2, 0, 1, 3);
    layout->addWidget(opts);

    m_preview = new QTreeWidget(this);
    m_preview->setHeaderLabels({QStringLiteral("Old name"), QStringLiteral("New name")});
    m_preview->setRootIsDecorated(false);
    m_preview->setUniformRowHeights(true);
    m_preview->setAlternatingRowColors(true);
    m_preview->header()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_preview, 1);
    m_summary = new QLabel(this);
    layout->addWidget(m_summary);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(QStringLiteral("Rename"));
    connect(m_buttons, &QDialogButtonBox::accepted, this, &AdvancedRenameDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &AdvancedRenameDialog::reject);
    layout->addWidget(m_buttons);

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(120);
    connect(&m_debounce, &QTimer::timeout, this, &AdvancedRenameDialog::refreshNow);
    for (QLineEdit *e : {m_old, m_new})
        connect(e, &QLineEdit::textChanged, this, &AdvancedRenameDialog::schedule);
    for (QCheckBox *c : {m_ignoreExt, m_caseSensitive, m_trim})
        connect(c, &QCheckBox::toggled, this, &AdvancedRenameDialog::schedule);
    for (QComboBox *c : {m_case, m_extCase})
        connect(c, &QComboBox::currentIndexChanged, this, &AdvancedRenameDialog::schedule);
    for (QSpinBox *b : {m_start, m_step, m_width})
        connect(b, &QSpinBox::valueChanged, this, &AdvancedRenameDialog::schedule);
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this]() {
        const int m = m_mode->currentIndex();
        m_oldLabel->setText(m == 0 ? QStringLiteral("Old name:") : m == 1 ? QStringLiteral("Find:") : QStringLiteral("Expression:"));
        m_newLabel->setText(m == 0 ? QStringLiteral("New name:") : QStringLiteral("Replace with:"));
        m_old->setPlaceholderText(m == 0 ? QStringLiteral("* (everything), or IMG*.JPG")
                                  : m == 1 ? QStringLiteral("Text to find")
                                           : QStringLiteral("^(\\w+) - (\\w+)$"));
        m_new->setPlaceholderText(m == 0 ? QStringLiteral("Holiday ###.*  (* keeps what * matched)")
                                  : m == 1 ? QStringLiteral("What to put instead")
                                           : QStringLiteral("\\2 - \\1"));
        m_caseSensitive->setEnabled(true);
        schedule();
    });

    connect(m_presets, &QComboBox::activated, this, [this](int index) {
        if (index <= 0)
            return;
        RenameOptions o;
        if (RenamePresets::load(m_presets->itemText(index), o))
            setOptions(o);
    });
    connect(savePreset, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const QString current = m_presets->currentIndex() > 0 ? m_presets->currentText() : QString();
        const QString name = QInputDialog::getText(this, QStringLiteral("Save Preset"), QStringLiteral("Name:"), QLineEdit::Normal, current, &ok);
        if (ok && RenamePresets::save(name, options()))
            refreshPresets(name.trimmed());
    });
    connect(deletePreset, &QPushButton::clicked, this, [this]() {
        if (m_presets->currentIndex() > 0 && RenamePresets::remove(m_presets->currentText()))
            refreshPresets();
    });

    refreshPresets();
    setOptions(initial);
    Q_EMIT m_mode->currentIndexChanged(m_mode->currentIndex());
    refreshNow();
    resize(620, 560);
    m_new->setFocus();
}

QPushButton *AdvancedRenameDialog::renameButton() const
{
    return m_buttons->button(QDialogButtonBox::Ok);
}

void AdvancedRenameDialog::refreshPresets(const QString &select)
{
    m_presets->clear();
    m_presets->addItem(QStringLiteral("(none)"));
    m_presets->addItems(RenamePresets::names());
    if (!select.isEmpty())
        m_presets->setCurrentText(select);
}

RenameOptions AdvancedRenameDialog::options() const
{
    RenameOptions o;
    o.mode = static_cast<RenameOptions::Mode>(m_mode->currentIndex());
    o.oldPattern = m_old->text();
    o.newPattern = m_new->text();
    o.ignoreExtension = m_ignoreExt->isChecked();
    o.caseSensitive = m_caseSensitive->isChecked();
    o.trimSpaces = m_trim->isChecked();
    o.nameCase = static_cast<RenameOptions::Case>(m_case->currentIndex());
    o.extCase = static_cast<RenameOptions::ExtCase>(m_extCase->currentIndex());
    o.counterStart = m_start->value();
    o.counterStep = m_step->value();
    o.counterWidth = m_width->value();
    return o;
}

void AdvancedRenameDialog::setOptions(const RenameOptions &o)
{
    m_loading = true;
    m_mode->setCurrentIndex(int(o.mode));
    m_old->setText(o.oldPattern);
    m_new->setText(o.newPattern);
    m_ignoreExt->setChecked(o.ignoreExtension);
    m_caseSensitive->setChecked(o.caseSensitive);
    m_trim->setChecked(o.trimSpaces);
    m_case->setCurrentIndex(int(o.nameCase));
    m_extCase->setCurrentIndex(int(o.extCase));
    m_start->setValue(o.counterStart);
    m_step->setValue(o.counterStep);
    m_width->setValue(o.counterWidth);
    m_loading = false;
    schedule();
}

void AdvancedRenameDialog::schedule()
{
    if (!m_loading)
        m_debounce.start();
}

void AdvancedRenameDialog::refreshNow()
{
    m_debounce.stop();
    m_items = RenameEngine::preview(m_paths, options());
    const Theme &theme = Style::instance()->theme();
    const QColor bad = QColor::fromRgb(0xd0, 0x40, 0x40);
    m_preview->setUpdatesEnabled(false);
    m_preview->clear();
    int clashes = 0;
    int unchanged = 0;
    for (const RenameItem &it : std::as_const(m_items)) {
        auto *row = new QTreeWidgetItem(m_preview, {it.oldName, it.newName});
        switch (it.status) {
        case RenameItem::Status::Ok:
            row->setForeground(1, theme.sourceAccent.isValid() ? theme.sourceAccent : palette().color(QPalette::Highlight));
            break;
        case RenameItem::Status::Conflict:
        case RenameItem::Status::Invalid:
            ++clashes;
            row->setForeground(1, bad);
            row->setToolTip(1, it.note);
            row->setText(1, it.newName + QStringLiteral("  (") + it.note + QLatin1Char(')'));
            break;
        default:
            ++unchanged;
            row->setForeground(0, theme.dimText);
            row->setForeground(1, theme.dimText);
            break;
        }
    }
    m_preview->setUpdatesEnabled(true);
    const int changes = RenameEngine::changeCount(m_items);
    QString text = QStringLiteral("%1 to rename").arg(changes);
    if (unchanged)
        text += QStringLiteral(", %1 unchanged").arg(unchanged);
    if (clashes)
        text += QStringLiteral(", <span style=\"color:%1\">%2 can't be renamed: fix them first</span>").arg(bad.name()).arg(clashes);
    m_summary->setText(text);
    renameButton()->setEnabled(changes > 0 && clashes == 0);
}

void AdvancedRenameDialog::accept()
{
    refreshNow(); // the files may have changed since the last preview
    if (!renameButton()->isEnabled())
        return;
    QString err;
    if (!RenameEngine::apply(m_items, &m_done, &err)) {
        QMessageBox::warning(this, QStringLiteral("Rename"), err);
        return;
    }
    QDialog::accept();
}
