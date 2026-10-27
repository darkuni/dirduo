#include "ui/KeysPage.h"

#include "ui/ButtonBar.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace {
enum Col { NameCol, KeyCol, TypeCol, CommandCol };
constexpr int RefKind = Qt::UserRole;
constexpr int RefId = Qt::UserRole + 1;
constexpr int RefIndex = Qt::UserRole + 2;

QString keysText(const QStringList &keys)
{
    QStringList shown;
    for (const QString &k : keys)
        shown << KeyMap::display(k);
    return shown.join(QStringLiteral(", "));
}

QString oneLine(QString s)
{
    s.replace(QLatin1Char('\n'), QStringLiteral("; "));
    return s;
}
} // namespace

KeysPage::KeysPage(ToolbarStore &toolbars, QWidget *parent)
    : QWidget(parent)
    , m_toolbars(toolbars)
{
    KeyMap *km = KeyMap::instance();
    for (const KeyMap::MenuItem &m : km->menuItems())
        m_menuKeys.insert(m.id, km->keysFor(m.id));
    m_hotkeys = km->hotkeys();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *intro = new QLabel(QStringLiteral("Every key in one place: hotkeys of your own that run any commands, the keys of the menu "
                                            "commands, and toolbar buttons' hotkeys. A key used twice shows in red."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("New Hotkey..."), this);
    add->setObjectName(QStringLiteral("keys-add"));
    m_duplicate = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-copy")), QStringLiteral("Duplicate"), this);
    m_edit = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), QStringLiteral("Edit..."), this);
    m_delete = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Delete"), this);
    m_reset = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-undo")), QStringLiteral("Default"), this);
    m_reset->setToolTip(QStringLiteral("Back to the key it came with"));
    auto *more = new QToolButton(this);
    more->setText(QStringLiteral("More"));
    more->setIcon(QIcon::fromTheme(QStringLiteral("application-menu")));
    more->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    more->setPopupMode(QToolButton::InstantPopup);
    auto *moreMenu = new QMenu(more);
    moreMenu->addAction(QStringLiteral("Export Hotkeys..."), this, &KeysPage::exportHotkeys);
    moreMenu->addAction(QStringLiteral("Import Hotkeys..."), this, &KeysPage::importHotkeys);
    moreMenu->addAction(QStringLiteral("Save a List of All Keys..."), this, &KeysPage::saveList);
    moreMenu->addSeparator();
    moreMenu->addAction(QStringLiteral("Put Every Menu Key Back as It Came"), this, &KeysPage::resetAll);
    more->setMenu(moreMenu);
    for (QWidget *w : {static_cast<QWidget *>(add), static_cast<QWidget *>(m_duplicate), static_cast<QWidget *>(m_edit),
                       static_cast<QWidget *>(m_delete), static_cast<QWidget *>(m_reset)})
        row->addWidget(w);
    row->addStretch(1);
    row->addWidget(more);
    layout->addLayout(row);

    m_list = new QTreeWidget(this);
    m_list->setObjectName(QStringLiteral("keys-list"));
    m_list->setRootIsDecorated(false);
    m_list->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Key"), QStringLiteral("Type"), QStringLiteral("Command")});
    m_list->header()->resizeSection(NameCol, 250);
    m_list->header()->resizeSection(KeyCol, 140);
    m_list->header()->resizeSection(TypeCol, 120);
    m_list->setSortingEnabled(true);
    m_list->sortByColumn(TypeCol, Qt::AscendingOrder);
    layout->addWidget(m_list, 1);

    auto *keyRow = new QHBoxLayout;
    keyRow->addWidget(new QLabel(QStringLiteral("Key:"), this));
    m_keyEdit = new QKeySequenceEdit(this);
    m_keyEdit->setObjectName(QStringLiteral("keys-edit"));
    m_keyEdit->setToolTip(QStringLiteral("Click here and press the key for the command picked above"));
    keyRow->addWidget(m_keyEdit, 1);
    m_clear = new QPushButton(QStringLiteral("No Key"), this);
    keyRow->addWidget(m_clear);
    keyRow->addSpacing(12);
    keyRow->addWidget(new QLabel(QStringLiteral("Filter:"), this));
    m_filter = new QLineEdit(this);
    m_filter->setPlaceholderText(QStringLiteral("Name, command or key"));
    m_filter->setClearButtonEnabled(true);
    keyRow->addWidget(m_filter, 1);
    layout->addLayout(keyRow);
    m_conflict = new QLabel(this);
    m_conflict->setWordWrap(true);
    m_conflict->setStyleSheet(QStringLiteral("color: #d04030"));
    layout->addWidget(m_conflict);

    connect(add, &QPushButton::clicked, this, [this]() { editHotkey(-1); });
    connect(m_edit, &QPushButton::clicked, this, [this]() {
        const Ref r = refOf(m_list->currentItem());
        if (r.kind == QLatin1String("hotkey"))
            editHotkey(r.index);
        else
            m_keyEdit->setFocus();
    });
    connect(m_list, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *it) {
        const Ref r = refOf(it);
        if (r.kind == QLatin1String("hotkey"))
            editHotkey(r.index);
        else
            m_keyEdit->setFocus();
    });
    connect(m_duplicate, &QPushButton::clicked, this, [this]() {
        const Ref r = refOf(m_list->currentItem());
        if (r.kind != QLatin1String("hotkey"))
            return;
        KeyMap::Hotkey h = m_hotkeys.value(r.index);
        h.name += QStringLiteral(" (copy)");
        h.key.clear();
        addHotkey(h);
    });
    connect(m_delete, &QPushButton::clicked, this, [this]() {
        const Ref r = refOf(m_list->currentItem());
        if (r.kind != QLatin1String("hotkey"))
            return;
        m_hotkeys.removeAt(r.index);
        fill();
    });
    connect(m_reset, &QPushButton::clicked, this, [this]() {
        const Ref r = refOf(m_list->currentItem());
        if (r.kind == QLatin1String("menu"))
            resetMenu(r.id);
    });
    connect(m_clear, &QPushButton::clicked, this, [this]() {
        if (m_list->currentItem())
            setKey(refOf(m_list->currentItem()), QString(), false);
    });
    connect(m_keyEdit, &QKeySequenceEdit::editingFinished, this, &KeysPage::changeKeyFromEditor);
    connect(m_list, &QTreeWidget::currentItemChanged, this, &KeysPage::updateButtons);
    connect(m_list, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *it, int col) {
        if (m_filling || col != NameCol)
            return;
        const Ref r = refOf(it);
        if (r.kind == QLatin1String("hotkey") && r.index < m_hotkeys.size()) {
            m_hotkeys[r.index].enabled = it->checkState(NameCol) == Qt::Checked;
            fill();
        }
    });
    connect(m_filter, &QLineEdit::textChanged, this, &KeysPage::setFilter);
    fill();
}

KeysPage::Ref KeysPage::refOf(const QTreeWidgetItem *item)
{
    if (!item)
        return {};
    return {item->data(NameCol, RefKind).toString(), item->data(NameCol, RefId).toString(), item->data(NameCol, RefIndex).toInt()};
}

QTreeWidgetItem *KeysPage::itemFor(const Ref &ref) const
{
    for (int i = 0; i < m_list->topLevelItemCount(); ++i)
        if (refOf(m_list->topLevelItem(i)) == ref)
            return m_list->topLevelItem(i);
    return nullptr;
}

QString KeysPage::nameOf(const Ref &ref) const
{
    if (ref.kind == QLatin1String("hotkey"))
        return m_hotkeys.value(ref.index).name;
    if (ref.kind == QLatin1String("menu"))
        return ref.id;
    const ToolbarDef bar = m_toolbars.toolbar(ref.id);
    const ButtonDef b = bar.buttons.value(ref.index);
    return QStringLiteral("%1 (%2 toolbar)").arg(b.label.isEmpty() ? b.tip : b.label, bar.name);
}

QStringList KeysPage::keysOf(const Ref &ref) const
{
    if (ref.kind == QLatin1String("hotkey")) {
        const QString k = m_hotkeys.value(ref.index).key;
        return k.isEmpty() ? QStringList() : QStringList{k};
    }
    if (ref.kind == QLatin1String("menu"))
        return m_menuKeys.value(ref.id);
    if (ref.kind == QLatin1String("button")) {
        const QString key = ref.id + QLatin1Char('|') + QString::number(ref.index);
        const QString k = m_buttonKeys.contains(key) ? m_buttonKeys.value(key) : m_toolbars.toolbar(ref.id).buttons.value(ref.index).key;
        return k.isEmpty() ? QStringList() : QStringList{k};
    }
    return {};
}

QHash<QString, QStringList> KeysPage::keyUsers() const
{
    QHash<QString, QStringList> users;
    for (int i = 0; i < m_hotkeys.size(); ++i)
        if (m_hotkeys.at(i).enabled && !m_hotkeys.at(i).key.isEmpty())
            users[m_hotkeys.at(i).key] << m_hotkeys.at(i).name;
    for (auto it = m_menuKeys.cbegin(); it != m_menuKeys.cend(); ++it)
        for (const QString &k : it.value())
            users[k] << it.key();
    for (const QString &id : m_toolbars.ids()) {
        const ToolbarDef bar = m_toolbars.toolbar(id);
        for (int i = 0; i < bar.buttons.size(); ++i) {
            const QStringList k = keysOf({QStringLiteral("button"), id, i});
            if (!k.isEmpty())
                users[k.first()] << nameOf({QStringLiteral("button"), id, i});
        }
    }
    return users;
}

QStringList KeysPage::conflicts() const
{
    QStringList out;
    const QHash<QString, QStringList> users = keyUsers();
    for (auto it = users.cbegin(); it != users.cend(); ++it)
        if (it.value().size() > 1)
            out << QStringLiteral("%1: %2").arg(KeyMap::display(it.key()), it.value().join(QStringLiteral(", ")));
    out.sort();
    return out;
}

void KeysPage::fill()
{
    m_filling = true;
    const Ref keep = refOf(m_list->currentItem());
    m_list->setSortingEnabled(false);
    m_list->clear();
    const QHash<QString, QStringList> users = keyUsers();
    auto addRow = [&](const Ref &ref, const QString &type, const QString &command) {
        auto *it = new QTreeWidgetItem(m_list);
        it->setData(NameCol, RefKind, ref.kind);
        it->setData(NameCol, RefId, ref.id);
        it->setData(NameCol, RefIndex, ref.index);
        it->setText(NameCol, nameOf(ref));
        const QStringList keys = keysOf(ref);
        it->setText(KeyCol, keysText(keys));
        it->setText(TypeCol, type);
        it->setText(CommandCol, oneLine(command));
        it->setToolTip(CommandCol, command);
        QStringList clash;
        for (const QString &k : keys) {
            QStringList others = users.value(k);
            if (others.size() < 2)
                continue;
            others.removeOne(nameOf(ref));
            clash << QStringLiteral("%1 is also used by %2").arg(KeyMap::display(k), others.join(QStringLiteral(", ")));
        }
        if (!clash.isEmpty()) {
            it->setForeground(KeyCol, QColor(0xd0, 0x40, 0x30));
            it->setToolTip(KeyCol, clash.join(QLatin1Char('\n')));
        }
        if (ref == keep)
            m_list->setCurrentItem(it);
        return it;
    };
    for (int i = 0; i < m_hotkeys.size(); ++i) {
        QTreeWidgetItem *it = addRow({QStringLiteral("hotkey"), QString(), i}, QStringLiteral("Hotkey"), m_hotkeys.at(i).command);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(NameCol, m_hotkeys.at(i).enabled ? Qt::Checked : Qt::Unchecked);
        if (!m_hotkeys.at(i).enabled)
            it->setForeground(KeyCol, palette().color(QPalette::Disabled, QPalette::Text));
    }
    for (const KeyMap::MenuItem &m : KeyMap::instance()->menuItems()) {
        QTreeWidgetItem *it = addRow({QStringLiteral("menu"), m.id, -1}, QStringLiteral("Menu"), m.command);
        if (m_menuKeys.value(m.id) != m.defaults) {
            QFont f = it->font(KeyCol);
            f.setBold(true);
            it->setFont(KeyCol, f);
            it->setToolTip(NameCol, m.defaults.isEmpty() ? QStringLiteral("Changed (it came with no key)")
                                                        : QStringLiteral("Changed (it came with %1)").arg(keysText(m.defaults)));
        }
    }
    for (const QString &id : m_toolbars.ids()) {
        const ToolbarDef bar = m_toolbars.toolbar(id);
        for (int i = 0; i < bar.buttons.size(); ++i) {
            const ButtonDef &b = bar.buttons.at(i);
            if (b.isItem() || (b.isMenu() && !b.isSplit()))
                continue;
            if (keysOf({QStringLiteral("button"), id, i}).isEmpty())
                continue; // as Opus: buttons with a hotkey (give one in the button's editor)
            addRow({QStringLiteral("button"), id, i}, QStringLiteral("Toolbar"), b.left);
        }
    }
    m_list->setSortingEnabled(true);
    setFilter(m_filter->text());
    const QStringList c = conflicts();
    m_conflict->setText(c.isEmpty() ? QString() : QStringLiteral("Used twice: ") + c.join(QStringLiteral("; ")));
    m_filling = false;
    updateButtons();
}

void KeysPage::setFilter(const QString &text)
{
    if (m_filter->text() != text)
        m_filter->setText(text);
    const QString t = text.trimmed();
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        QTreeWidgetItem *it = m_list->topLevelItem(i);
        bool show = t.isEmpty();
        for (int c = 0; c < 4 && !show; ++c)
            show = it->text(c).contains(t, Qt::CaseInsensitive);
        it->setHidden(!show);
    }
}

void KeysPage::updateButtons()
{
    const Ref r = refOf(m_list->currentItem());
    const bool hot = r.kind == QLatin1String("hotkey");
    m_edit->setEnabled(!r.kind.isEmpty());
    m_duplicate->setEnabled(hot);
    m_delete->setEnabled(hot);
    m_reset->setEnabled(r.kind == QLatin1String("menu") && KeyMap::instance()->menuItem(r.id).defaults != m_menuKeys.value(r.id));
    m_keyEdit->setEnabled(!r.kind.isEmpty());
    m_clear->setEnabled(!r.kind.isEmpty() && !keysOf(r).isEmpty());
    const QSignalBlocker block(m_keyEdit);
    const QStringList keys = keysOf(r);
    m_keyEdit->setKeySequence(keys.isEmpty() ? QKeySequence() : QKeySequence(keys.first(), QKeySequence::PortableText));
}

void KeysPage::addHotkey(const KeyMap::Hotkey &h)
{
    m_hotkeys << h;
    fill();
    if (QTreeWidgetItem *it = itemFor({QStringLiteral("hotkey"), QString(), int(m_hotkeys.size()) - 1}))
        m_list->setCurrentItem(it);
}

void KeysPage::setKey(const Ref &ref, const QString &key, bool take)
{
    if (ref.kind.isEmpty())
        return;
    if (take && !key.isEmpty()) {
        // Take it from everything else that has it.
        for (int i = 0; i < m_hotkeys.size(); ++i)
            if (!(ref.kind == QLatin1String("hotkey") && ref.index == i) && m_hotkeys.at(i).key == key)
                m_hotkeys[i].key.clear();
        for (auto it = m_menuKeys.begin(); it != m_menuKeys.end(); ++it)
            if (!(ref.kind == QLatin1String("menu") && ref.id == it.key()))
                it.value().removeAll(key);
        for (const QString &id : m_toolbars.ids()) {
            const ToolbarDef bar = m_toolbars.toolbar(id);
            for (int i = 0; i < bar.buttons.size(); ++i)
                if (!(ref.kind == QLatin1String("button") && ref.id == id && ref.index == i)
                    && keysOf({QStringLiteral("button"), id, i}).value(0) == key)
                    m_buttonKeys.insert(id + QLatin1Char('|') + QString::number(i), QString());
        }
    }
    if (ref.kind == QLatin1String("hotkey") && ref.index < m_hotkeys.size()) {
        m_hotkeys[ref.index].key = key;
    } else if (ref.kind == QLatin1String("menu")) {
        // The new key first; the menu shows the first one.
        QStringList keys = m_menuKeys.value(ref.id);
        if (key.isEmpty()) {
            keys.clear();
        } else {
            keys.removeAll(key);
            if (!keys.isEmpty())
                keys.removeFirst(); // replaces the main key, keeps a second one
            keys.prepend(key);
        }
        m_menuKeys.insert(ref.id, keys);
    } else if (ref.kind == QLatin1String("button")) {
        m_buttonKeys.insert(ref.id + QLatin1Char('|') + QString::number(ref.index), key);
    }
    fill();
}

void KeysPage::changeKeyFromEditor()
{
    QTreeWidgetItem *cur = m_list->currentItem();
    if (!cur)
        return;
    const QKeySequence seq = m_keyEdit->keySequence();
    if (seq.isEmpty())
        return;
    const QString key = KeyMap::portable(QKeySequence(seq[0])); // one key, not a chord
    const Ref r = refOf(cur);
    if (keysOf(r).value(0) == key)
        return;
    QStringList others = keyUsers().value(key);
    others.removeAll(nameOf(r));
    bool take = false;
    if (!others.isEmpty()) {
        const auto answer = QMessageBox::question(this, QStringLiteral("Key in Use"),
                                                  QStringLiteral("%1 is used by %2.\n\nGive it to %3 instead?")
                                                      .arg(KeyMap::display(key), others.join(QStringLiteral(", ")), nameOf(r)),
                                                  QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel, QMessageBox::Yes);
        if (answer == QMessageBox::Cancel) {
            updateButtons();
            return;
        }
        take = answer == QMessageBox::Yes;
    }
    setKey(r, key, take);
}

void KeysPage::resetMenu(const QString &id)
{
    m_menuKeys.insert(id, KeyMap::instance()->menuItem(id).defaults);
    fill();
}

void KeysPage::resetAll()
{
    for (const KeyMap::MenuItem &m : KeyMap::instance()->menuItems())
        m_menuKeys.insert(m.id, m.defaults);
    fill();
}

void KeysPage::editHotkey(int index)
{
    KeyMap::Hotkey h = index >= 0 ? m_hotkeys.value(index) : KeyMap::Hotkey();
    QDialog dlg(this);
    dlg.setWindowTitle(index >= 0 ? QStringLiteral("Edit Hotkey") : QStringLiteral("New Hotkey"));
    auto *form = new QFormLayout(&dlg);
    auto *name = new QLineEdit(h.name, &dlg);
    name->setPlaceholderText(QStringLiteral("What it does"));
    auto *key = new QKeySequenceEdit(QKeySequence(h.key, QKeySequence::PortableText), &dlg);
    auto *cmd = new QPlainTextEdit(h.command, &dlg);
    cmd->setPlaceholderText(QStringLiteral("Commands, one per line, as on a button:\nGo /downloads\nSelect PATTERN=*.zip"));
    cmd->setMinimumHeight(fontMetrics().height() * 6);
    form->addRow(QStringLiteral("Name:"), name);
    form->addRow(QStringLiteral("Key:"), key);
    form->addRow(QStringLiteral("Commands:"), cmd);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted || cmd->toPlainText().trimmed().isEmpty())
        return;
    h.name = name->text().trimmed().isEmpty() ? cmd->toPlainText().trimmed().section(QLatin1Char('\n'), 0, 0) : name->text().trimmed();
    h.command = cmd->toPlainText().trimmed();
    const QString newKey = key->keySequence().isEmpty() ? QString() : KeyMap::portable(QKeySequence(key->keySequence()[0]));
    h.key.clear();
    int at = index;
    if (index >= 0)
        m_hotkeys[index] = h;
    else {
        m_hotkeys << h;
        at = int(m_hotkeys.size()) - 1;
    }
    fill();
    if (QTreeWidgetItem *it = itemFor({QStringLiteral("hotkey"), QString(), at}))
        m_list->setCurrentItem(it);
    if (!newKey.isEmpty()) {
        const QSignalBlocker block(m_keyEdit);
        m_keyEdit->setKeySequence(QKeySequence(newKey, QKeySequence::PortableText));
        changeKeyFromEditor(); // asks when the key is in use
    }
}

bool KeysPage::apply(QString *error)
{
    Q_UNUSED(error)
    // Toolbar buttons keep their keys in their toolbars.
    QHash<QString, QList<QPair<int, QString>>> perBar;
    for (auto it = m_buttonKeys.cbegin(); it != m_buttonKeys.cend(); ++it)
        perBar[it.key().section(QLatin1Char('|'), 0, 0)] << qMakePair(it.key().section(QLatin1Char('|'), 1).toInt(), it.value());
    for (auto it = perBar.cbegin(); it != perBar.cend(); ++it) {
        const ToolbarDef bar = m_toolbars.toolbar(it.key());
        for (const auto &p : it.value()) {
            if (p.first < 0 || p.first >= bar.buttons.size() || bar.buttons.at(p.first).key == p.second)
                continue;
            ButtonDef b = bar.buttons.at(p.first);
            b.key = p.second;
            m_toolbars.replaceButton(it.key(), p.first, b);
        }
    }
    m_buttonKeys.clear();
    KeyMap::instance()->set(m_menuKeys, m_hotkeys);
    fill();
    return true;
}

void KeysPage::exportHotkeys()
{
    const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Export Hotkeys"), QStringLiteral("dirduo-hotkeys.json"),
                                                      QStringLiteral("DirDuo hotkeys (*.json)"));
    if (file.isEmpty())
        return;
    QString err;
    if (!KeyMap::exportHotkeys(file, m_hotkeys, &err))
        QMessageBox::warning(this, QStringLiteral("Export Hotkeys"), err);
}

void KeysPage::importHotkeys()
{
    const QString file = QFileDialog::getOpenFileName(this, QStringLiteral("Import Hotkeys"), QString(), QStringLiteral("DirDuo hotkeys (*.json)"));
    if (file.isEmpty())
        return;
    bool ok = false;
    QString err;
    const QList<KeyMap::Hotkey> got = KeyMap::importHotkeys(file, &ok, &err);
    if (!ok) {
        QMessageBox::warning(this, QStringLiteral("Import Hotkeys"), err);
        return;
    }
    QMessageBox box(QMessageBox::Question, QStringLiteral("Import Hotkeys"),
                    QStringLiteral("%1 hotkeys in the file. Add them to yours, or replace yours with them?").arg(got.size()),
                    QMessageBox::Cancel, this);
    QPushButton *merge = box.addButton(QStringLiteral("Add Them"), QMessageBox::AcceptRole);
    QPushButton *replace = box.addButton(QStringLiteral("Replace Mine"), QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() == replace)
        m_hotkeys = got;
    else if (box.clickedButton() == merge)
        for (const KeyMap::Hotkey &h : got)
            if (!m_hotkeys.contains(h))
                m_hotkeys << h;
    fill();
}

void KeysPage::saveList()
{
    // A list to print or keep: every key, as Opus's CSV export.
    const QString file = QFileDialog::getSaveFileName(this, QStringLiteral("Save a List of All Keys"), QStringLiteral("dirduo-keys.csv"),
                                                      QStringLiteral("CSV (*.csv)"));
    if (file.isEmpty())
        return;
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, QStringLiteral("Save List"), f.errorString());
        return;
    }
    auto q = [](QString s) { return QLatin1Char('"') + s.replace(QLatin1Char('"'), QStringLiteral("\"\"")) + QLatin1Char('"'); };
    QString out = QStringLiteral("Key,Name,Type,Command\n");
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *it = m_list->topLevelItem(i);
        if (it->text(KeyCol).isEmpty())
            continue;
        out += QStringList{q(it->text(KeyCol)), q(it->text(NameCol)), q(it->text(TypeCol)), q(it->text(CommandCol))}.join(QLatin1Char(','))
            + QLatin1Char('\n');
    }
    f.write(out.toUtf8());
    if (!f.commit())
        QMessageBox::warning(this, QStringLiteral("Save List"), f.errorString());
}
