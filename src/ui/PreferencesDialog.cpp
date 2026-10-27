#include "ui/PreferencesDialog.h"
#include <QSpinBox>
#include "core/TabGroups.h"
#include "core/SavedFilters.h"
#include "ui/KeysPage.h"
#include <QDoubleSpinBox>
#include <QUrl>
#include <QDesktopServices>
#include <QInputDialog>
#include "core/Scripting.h"
#include <QTextCursor>
#include <QToolButton>
#include <QPlainTextEdit>
#include <QFontDatabase>
#include "ui/FolderTab.h"
#include "core/StatusFormat.h"
#include "ui/StatusLine.h"
#include <QSet>
#include <QMimeDatabase>
#include "core/Apps.h"
#include "core/Prefs.h"

#include "core/AliasManager.h"
#include "core/FileKind.h"
#include "core/Paths.h"
#include "ui/AppContext.h"
#include "ui/ButtonBar.h"
#include "ui/FolderFormats.h"
#include "ui/ListerWindow.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

// ---------------------------------------------------------------- ColorButton

ColorButton::ColorButton(QWidget *parent)
    : QToolButton(parent)
{
    setFixedSize(46, 24);
    connect(this, &QToolButton::clicked, this, [this]() {
        const QColor c = QColorDialog::getColor(m_color.isValid() ? m_color : Qt::white, this);
        if (c.isValid())
            setColor(c), Q_EMIT colorChanged(c);
    });
}

void ColorButton::setColor(const QColor &c)
{
    m_color = c;
    QPixmap pm(34, 14);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setPen(palette().color(QPalette::Mid));
    if (c.isValid()) {
        p.setBrush(c);
        p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
    } else {
        p.drawRect(pm.rect().adjusted(0, 0, -1, -1));
        p.drawLine(1, pm.height() - 2, pm.width() - 2, 1); // none
    }
    p.end();
    setIcon(QIcon(pm));
    setIconSize(pm.size());
    setToolTip(c.isValid() ? c.name() : QStringLiteral("None"));
}

void ColorButton::contextMenuEvent(QContextMenuEvent *event)
{
    if (!m_allowNone)
        return QToolButton::contextMenuEvent(event);
    QMenu menu(this);
    connect(menu.addAction(QStringLiteral("No Color")), &QAction::triggered, this, [this]() {
        setColor(QColor());
        Q_EMIT colorChanged(QColor());
    });
    menu.exec(event->globalPos());
}

// ---------------------------------------------------------------- helpers

namespace {

struct ColorSlot
{
    const char *key;
    const char *label;
    QColor Theme::*member;
};

const QList<ColorSlot> &colorSlots()
{
    static const QList<ColorSlot> slots = {
        {"window", "Window", &Theme::window},
        {"base", "File list", &Theme::base},
        {"alternate", "Every other row", &Theme::alternate},
        {"text", "Text", &Theme::text},
        {"dimText", "Hidden and dimmed text", &Theme::dimText},
        {"highlight", "Selection", &Theme::highlight},
        {"highlightText", "Selected text", &Theme::highlightText},
        {"button", "Buttons", &Theme::button},
        {"sourceAccent", "SOURCE", &Theme::sourceAccent},
        {"destAccent", "DEST", &Theme::destAccent},
        {"lockedAccent", "Locked DEST", &Theme::lockedAccent},
        {"groupHeader", "Group headings", &Theme::groupHeader},
        {"sizeBar", "Folder size bars", &Theme::sizeBar},
    };
    return slots;
}

QList<FileKind::Category> typeCategories()
{
    using C = FileKind::Category;
    return {C::Folder, C::Image, C::Audio, C::Video, C::Archive, C::DiskImage, C::Document, C::Code, C::Executable};
}

QString safeFileName(QString name)
{
    for (QChar &c : name) {
        if (!c.isLetterOrNumber() && c != QLatin1Char(' ') && c != QLatin1Char('-') && c != QLatin1Char('_'))
            c = QLatin1Char('_');
    }
    return name.trimmed();
}

} // namespace

// ---------------------------------------------------------------- dialog

QStringList PreferencesDialog::pageNames()
{
    return {QStringLiteral("general"), QStringLiteral("appearance"), QStringLiteral("theme"), QStringLiteral("labels"),
            QStringLiteral("aliases"), QStringLiteral("formats"), QStringLiteral("filetypes"), QStringLiteral("toolbars"), QStringLiteral("statusbar"), QStringLiteral("scripts"), QStringLiteral("viewer"), QStringLiteral("keys"), QStringLiteral("filters"), QStringLiteral("tabgroups")};
}

PreferencesDialog::PreferencesDialog(AppContext &app, ListerWindow *lister, const QString &page)
    : QDialog(lister)
    , m_app(app)
    , m_lister(lister)
{
    setWindowTitle(QStringLiteral("Preferences"));
    m_originalTheme = Style::instance()->theme().name;
    m_originalThemeData = Style::instance()->theme();
    m_originalSize = Style::instance()->sizeLevel();

    auto *layout = new QVBoxLayout(this);
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText(QStringLiteral("Search settings (color, alias, label, size...)"));
    m_search->setClearButtonEnabled(true);
    layout->addWidget(m_search);
    auto *body = new QHBoxLayout;
    m_nav = new QListWidget(this);
    m_nav->setFixedWidth(170);
    m_nav->setIconSize(QSize(20, 20));
    m_stack = new QStackedWidget(this);
    body->addWidget(m_nav);
    body->addWidget(m_stack, 1);
    layout->addLayout(body, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        QString err;
        if (!apply(&err))
            QMessageBox::warning(this, QStringLiteral("Preferences"), err);
    });
    layout->addWidget(buttons);

    addPage(QStringLiteral("general"), QStringLiteral("General"), QStringLiteral("preferences-system"), buildGeneral(),
            {QStringLiteral("terminal"), QStringLiteral("konsole"), QStringLiteral("junk"),
             QStringLiteral("retry"), QStringLiteral("skip"), QStringLiteral("error")});
    addPage(QStringLiteral("appearance"), QStringLiteral("Appearance"), QStringLiteral("preferences-desktop-theme"), buildAppearance(),
            {QStringLiteral("theme"), QStringLiteral("size"), QStringLiteral("font"), QStringLiteral("icon"), QStringLiteral("dark"),
             QStringLiteral("light"), QStringLiteral("workbench"), QStringLiteral("scale")});
    addPage(QStringLiteral("theme"), QStringLiteral("Theme Editor"), QStringLiteral("color-management"), buildTheme(),
            {QStringLiteral("color"), QStringLiteral("colour"), QStringLiteral("accent"), QStringLiteral("file type"),
             QStringLiteral("skin"), QStringLiteral("palette")});
    addPage(QStringLiteral("labels"), QStringLiteral("Labels"), QStringLiteral("tag"), buildLabels(),
            {QStringLiteral("wildcard"), QStringLiteral("bold"), QStringLiteral("italic"), QStringLiteral("highlight"), QStringLiteral("color"),
             QStringLiteral("color")});
    addPage(QStringLiteral("aliases"), QStringLiteral("Aliases"), QStringLiteral("folder-bookmark"), buildAliases(),
            {QStringLiteral("shortcut"), QStringLiteral("path"), QStringLiteral("folder"), QStringLiteral("server")});
    addPage(QStringLiteral("formats"), QStringLiteral("Folder Formats"), QStringLiteral("view-list-details"), buildFormats(),
            {QStringLiteral("columns"), QStringLiteral("view"), QStringLiteral("group"), QStringLiteral("icons"), QStringLiteral("pictures")});
    addPage(QStringLiteral("filetypes"), QStringLiteral("File Types"), QStringLiteral("preferences-desktop-filetype-association"), buildFileTypes(),
            {QStringLiteral("open with"), QStringLiteral("application"), QStringLiteral("default"), QStringLiteral("program")});
    addPage(QStringLiteral("viewer"), QStringLiteral("Viewer"), QStringLiteral("view-preview"), buildViewer(),
            {QStringLiteral("picture"), QStringLiteral("image"), QStringLiteral("slideshow"), QStringLiteral("music"), QStringLiteral("video"),
             QStringLiteral("player"), QStringLiteral("show"), QStringLiteral("mark"), QStringLiteral("zoom"), QStringLiteral("wheel")});
    addPage(QStringLiteral("filters"), QStringLiteral("Filters"), QStringLiteral("view-filter"), buildFilters(),
            {QStringLiteral("filter"), QStringLiteral("saved"), QStringLiteral("wildcard"), QStringLiteral("pattern")});
    addPage(QStringLiteral("tabgroups"), QStringLiteral("Tab Groups"), QStringLiteral("tab-duplicate"), buildTabGroups(),
            {QStringLiteral("tab"), QStringLiteral("group"), QStringLiteral("tabs"), QStringLiteral("session")});
    m_keysPage = new KeysPage(m_app.toolbars());
    addPage(QStringLiteral("keys"), QStringLiteral("Keys"), QStringLiteral("input-keyboard"), m_keysPage,
            {QStringLiteral("keyboard"), QStringLiteral("shortcut"), QStringLiteral("hotkey"), QStringLiteral("key"), QStringLiteral("accelerator")});
    addPage(QStringLiteral("toolbars"), QStringLiteral("Toolbars"), QStringLiteral("configure-toolbars"), buildToolbars(),
            {QStringLiteral("button"), QStringLiteral("customize"), QStringLiteral("customize")});
    addPage(QStringLiteral("scripts"), QStringLiteral("Scripts"), QStringLiteral("text-x-script"), buildScripts(),
            {QStringLiteral("javascript"), QStringLiteral("script"), QStringLiteral("command"), QStringLiteral("column"),
             QStringLiteral("event"), QStringLiteral("automation")});
    addPage(QStringLiteral("statusbar"), QStringLiteral("Status Bar"), QStringLiteral("view-list-details"), buildStatusBar(),
            {QStringLiteral("status"), QStringLiteral("free space"), QStringLiteral("codes"), QStringLiteral("definition"),
             QStringLiteral("bar graph"), QStringLiteral("selected"), QStringLiteral("count")});

    connect(m_nav, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0)
            m_stack->setCurrentIndex(m_nav->item(row)->data(Qt::UserRole).toInt());
    });
    connect(m_search, &QLineEdit::textChanged, this, [this](const QString &t) { search(t); });
    m_nav->setCurrentRow(0);
    if (!page.isEmpty())
        setPage(page);
    resize(760, 560);
}

PreferencesDialog::~PreferencesDialog() = default;

QWidget *PreferencesDialog::buildGeneral()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    const Prefs::Values &v = Prefs::instance()->values();
    auto box = [&](const QString &key, const QString &text, bool on, QLayout *into, QWidget *parent) {
        auto *b = new QCheckBox(text, parent);
        b->setChecked(on);
        into->addWidget(b);
        m_general.insert(key, b);
    };
    auto *copies = new QGroupBox(QStringLiteral("Copying"), page);
    auto *cl = new QVBoxLayout(copies);
    box(QStringLiteral("ask"), QStringLiteral("Ask Retry / Skip / Abort when an item fails (otherwise skip it and list it at the end)"),
        v.askOnError, cl, copies);
    auto *notifyRow = new QHBoxLayout;
    box(QStringLiteral("notify"), QStringLiteral("A desktop notice when a job finishes while DirDuo is in the background, if it took over"),
        v.notifyJobs, notifyRow, copies);
    m_notifyAfter = new QSpinBox(copies);
    m_notifyAfter->setRange(0, 3600);
    m_notifyAfter->setSuffix(QStringLiteral(" seconds"));
    m_notifyAfter->setValue(v.notifyAfter);
    notifyRow->addWidget(m_notifyAfter);
    notifyRow->addStretch(1);
    cl->addLayout(notifyRow);
    layout->addWidget(copies);
    auto *files = new QGroupBox(QStringLiteral("Files"), page);
    auto *fl = new QVBoxLayout(files);
    box(QStringLiteral("junk"), QStringLiteral("Treat .DS_Store, ._ files, Thumbs.db, desktop.ini and __MACOSX as hidden files"), v.junkHidden, fl, files);
    box(QStringLiteral("treelines"), QStringLiteral("Folder trees: dotted branch lines and + / - boxes"), v.treeLines, fl, files);
    box(QStringLiteral("treepath"), QStringLiteral("Folder trees: draw the way to the current folder in the display's color"), v.treePath, fl, files);
    layout->addWidget(files);
    auto *term = new QGroupBox(QStringLiteral("Terminal"), page);
    auto *tl = new QFormLayout(term);
    m_terminal = new QComboBox(term);
    m_terminal->setEditable(true);
    m_terminal->addItem(QStringLiteral("Konsole (or the first one installed)"), QString());
    for (const char *t : {"konsole", "gnome-terminal", "kgx", "xfce4-terminal", "tilix", "alacritty", "kitty", "foot", "xterm"})
        m_terminal->addItem(QString::fromLatin1(t), QString::fromLatin1(t));
    const int at = m_terminal->findData(v.terminal);
    if (at >= 0)
        m_terminal->setCurrentIndex(at);
    else
        m_terminal->setEditText(v.terminal);
    tl->addRow(QStringLiteral("Open Konsole Here uses:"), m_terminal);
    layout->addWidget(term);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildViewer()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    const Prefs::Values::Viewer &v = Prefs::instance()->values().viewer;
    auto box = [&](const QString &key, const QString &text, bool on, QLayout *into, QWidget *parent) {
        auto *b = new QCheckBox(text, parent);
        b->setChecked(on);
        into->addWidget(b);
        m_viewerBoxes.insert(key, b);
    };
    auto *intro = new QLabel(QStringLiteral("The standalone viewer: right-click a file > View (or Play), the Show command, or double-click "
                                            "when turned on below. F3's viewer pane is separate."),
                             page);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *open = new QGroupBox(QStringLiteral("Opening"), page);
    auto *ol = new QVBoxLayout(open);
    box(QStringLiteral("pictures"), QStringLiteral("Double-clicking a picture opens it in the viewer"), v.openPictures, ol, open);
    box(QStringLiteral("media"), QStringLiteral("Double-clicking music or a video plays it in the viewer"), v.openMedia, ol, open);
    box(QStringLiteral("siblings"), QStringLiteral("Next and Previous go through the other files of the same kind in the folder"), v.siblings, ol, open);
    box(QStringLiteral("reuse"), QStringLiteral("Use the viewer that is already open"), v.reuse, ol, open);
    layout->addWidget(open);

    auto *look = new QGroupBox(QStringLiteral("Pictures"), page);
    auto *lf = new QFormLayout(look);
    m_viewerZoom = new QComboBox(look);
    m_viewerZoom->addItems({QStringLiteral("Fit to the window (shrink big ones only)"), QStringLiteral("Grow to the window"),
                            QStringLiteral("Original size")});
    m_viewerZoom->setCurrentIndex(v.resetZoom);
    lf->addRow(QStringLiteral("Each picture opens at:"), m_viewerZoom);
    m_viewerBackground = new QComboBox(look);
    m_viewerBackground->addItems({QStringLiteral("Dark"), QStringLiteral("Black"), QStringLiteral("White"), QStringLiteral("The theme's"),
                                  QStringLiteral("Checkerboard")});
    m_viewerBackground->setCurrentIndex(v.background);
    lf->addRow(QStringLiteral("Background:"), m_viewerBackground);
    auto *wheel = new QComboBox(look);
    wheel->setObjectName(QStringLiteral("viewer-wheel"));
    wheel->addItems({QStringLiteral("Goes to the next or previous file (Ctrl+wheel zooms)"), QStringLiteral("Zooms")});
    wheel->setCurrentIndex(v.wheelSteps ? 0 : 1);
    m_viewerWheel = wheel;
    lf->addRow(QStringLiteral("The mouse wheel:"), wheel);
    auto *lb = new QVBoxLayout;
    box(QStringLiteral("wrap"), QStringLiteral("After the last file, go round to the first"), v.wrap, lb, look);
    box(QStringLiteral("fullpath"), QStringLiteral("Show the full path in the title"), v.fullPathTitle, lb, look);
    lf->addRow(lb);
    layout->addWidget(look);

    auto *play = new QGroupBox(QStringLiteral("Music and video"), page);
    auto *pl = new QVBoxLayout(play);
    box(QStringLiteral("autoplay"), QStringLiteral("Start playing when shown"), v.autoPlay, pl, play);
    box(QStringLiteral("playnext"), QStringLiteral("Play the next file when one ends"), v.playNext, pl, play);
    layout->addWidget(play);

    auto *slide = new QGroupBox(QStringLiteral("Slideshow (S)"), page);
    auto *sf = new QFormLayout(slide);
    m_slideSeconds = new QDoubleSpinBox(slide);
    m_slideSeconds->setRange(0.2, 3600);
    m_slideSeconds->setDecimals(1);
    m_slideSeconds->setSingleStep(0.5);
    m_slideSeconds->setSuffix(QStringLiteral(" seconds"));
    m_slideSeconds->setValue(v.slideSeconds);
    sf->addRow(QStringLiteral("Each picture shows for:"), m_slideSeconds);
    auto *sb = new QVBoxLayout;
    box(QStringLiteral("random"), QStringLiteral("In random order"), v.slideRandom, sb, slide);
    box(QStringLiteral("slideauto"), QStringLiteral("Start one whenever the viewer opens with several pictures"), v.slideAuto, sb, slide);
    sf->addRow(sb);
    layout->addWidget(slide);

    auto *mark = new QGroupBox(QStringLiteral("Marking (M)"), page);
    auto *mf = new QFormLayout(mark);
    m_markName = new QLineEdit(v.markCollection, mark);
    m_markName->setToolTip(QStringLiteral("%F is the folder's name, %D today's date"));
    mf->addRow(QStringLiteral("Marked files go in the collection:"), m_markName);
    auto *mb = new QVBoxLayout;
    box(QStringLiteral("openmarked"), QStringLiteral("Open the collection in a tab when the viewer closes"), v.openMarkedOnClose, mb, mark);
    mf->addRow(mb);
    layout->addWidget(mark);
    layout->addStretch(1);
    return page;
}

QWidget *PreferencesDialog::buildFilters()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *intro = new QLabel(QStringLiteral("Filters kept under a name: pick one from the filter bar's Saved button, View > Saved Filters, "
                                            "a toolbar button (Set FILTER=@name) or Select PATTERN=@name. Separate wildcards with ; "
                                            "(*.iso;*.chd). Double-click a cell to change it."),
                             page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_filterList = new QTreeWidget(page);
    m_filterList->setObjectName(QStringLiteral("filter-list"));
    m_filterList->setRootIsDecorated(false);
    m_filterList->setHeaderLabels({QStringLiteral("Name"), QStringLiteral("Wildcards"), QStringLiteral("Hide matches"), QStringLiteral("Applies to")});
    m_filterList->header()->resizeSection(0, 150);
    m_filterList->header()->resizeSection(1, 360);
    m_filterList->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    auto addRow = [this](const SavedFilters::Filter &f) {
        auto *it = new QTreeWidgetItem(m_filterList, {f.name, f.pattern, QString(), f.scope});
        it->setFlags(it->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        it->setCheckState(2, f.hide ? Qt::Checked : Qt::Unchecked);
        return it;
    };
    for (const SavedFilters::Filter &f : SavedFilters::instance()->filters())
        addRow(f);
    layout->addWidget(m_filterList, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QIcon::fromTheme(QStringLiteral("list-add")), QStringLiteral("New"), page);
    auto *del = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Delete"), page);
    auto *defaults = new QPushButton(QStringLiteral("Add the Ready-Made Ones"), page);
    row->addWidget(add);
    row->addWidget(del);
    row->addStretch(1);
    row->addWidget(defaults);
    layout->addLayout(row);
    connect(add, &QPushButton::clicked, this, [this, addRow]() {
        SavedFilters::Filter f;
        f.name = QStringLiteral("New filter");
        f.pattern = QStringLiteral("*.txt");
        QTreeWidgetItem *it = addRow(f);
        m_filterList->setCurrentItem(it);
        m_filterList->editItem(it, 0);
    });
    connect(del, &QPushButton::clicked, this, [this]() { delete m_filterList->currentItem(); });
    connect(defaults, &QPushButton::clicked, this, [this, addRow]() {
        QStringList have;
        for (int i = 0; i < m_filterList->topLevelItemCount(); ++i)
            have << m_filterList->topLevelItem(i)->text(0).toLower();
        for (const SavedFilters::Filter &f : SavedFilters::defaults())
            if (!have.contains(f.name.toLower()))
                addRow(f);
    });
    // "Applies to" is files, folders or both.
    connect(m_filterList, &QTreeWidget::itemChanged, this, [](QTreeWidgetItem *it, int col) {
        if (col != 3)
            return;
        const QString v = it->text(3).trimmed().toLower();
        const QString fixed = v.startsWith(QLatin1Char('d')) || v.startsWith(QLatin1String("fo")) ? QStringLiteral("folders")
            : v.startsWith(QLatin1Char('b'))                                                   ? QStringLiteral("both")
                                                                                               : QStringLiteral("files");
        if (fixed != it->text(3))
            it->setText(3, fixed);
    });
    return page;
}

QWidget *PreferencesDialog::buildTabGroups()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *intro = new QLabel(QStringLiteral("The tabs of both displays, saved under a name to open again all at once: Go > Tab Groups, "
                                            "or a button with Go TABGROUPLOAD=name. Go > Tab Groups > Save These Tabs as a Group makes one. "
                                            "Double-click a name to rename it."),
                             page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_groupList = new QTreeWidget(page);
    m_groupList->setObjectName(QStringLiteral("tabgroup-list"));
    m_groupList->setHeaderLabels({QStringLiteral("Tab group"), QStringLiteral("Tabs"), QStringLiteral("Close the open tabs first")});
    m_groupList->header()->resizeSection(0, 200);
    m_groupList->header()->resizeSection(1, 90);
    m_groupList->setEditTriggers(QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed);
    for (const TabGroups::Group &g : TabGroups::instance()->groups()) {
        auto *it = new QTreeWidgetItem(m_groupList, {g.name, g.right.isEmpty() ? QString::number(g.left.size())
                                                                              : QStringLiteral("%1 + %2").arg(g.left.size()).arg(g.right.size())});
        it->setFlags(it->flags() | Qt::ItemIsEditable | Qt::ItemIsUserCheckable);
        it->setCheckState(2, g.closeOthers ? Qt::Checked : Qt::Unchecked);
        it->setData(0, Qt::UserRole, g.name);
        auto addSide = [it](const QString &title, const QStringList &paths, int current) {
            auto *side = new QTreeWidgetItem(it, {title});
            side->setFlags(side->flags() & ~Qt::ItemIsEditable);
            for (int i = 0; i < paths.size(); ++i) {
                auto *t = new QTreeWidgetItem(side, {paths.at(i)});
                if (i == current) {
                    QFont f = t->font(0);
                    f.setBold(true);
                    t->setFont(0, f);
                }
            }
        };
        addSide(g.right.isEmpty() ? QStringLiteral("Tabs") : QStringLiteral("First display"), g.left, g.leftCurrent);
        if (!g.right.isEmpty())
            addSide(QStringLiteral("Second display"), g.right, g.rightCurrent);
    }
    layout->addWidget(m_groupList, 1);
    auto *row = new QHBoxLayout;
    auto *del = new QPushButton(QIcon::fromTheme(QStringLiteral("list-remove")), QStringLiteral("Delete"), page);
    auto *open = new QPushButton(QIcon::fromTheme(QStringLiteral("tab-new")), QStringLiteral("Open Now"), page);
    auto *update = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Replace with the Tabs Open Now"), page);
    row->addWidget(open);
    row->addWidget(update);
    row->addWidget(del);
    row->addStretch(1);
    layout->addLayout(row);
    auto topOf = [](QTreeWidgetItem *it) {
        while (it && it->parent())
            it = it->parent();
        return it;
    };
    connect(del, &QPushButton::clicked, this, [this, topOf]() { delete topOf(m_groupList->currentItem()); });
    connect(open, &QPushButton::clicked, this, [this, topOf]() {
        if (QTreeWidgetItem *it = topOf(m_groupList->currentItem()); it && m_lister) {
            QString n = it->data(0, Qt::UserRole).toString();
            n.remove(QLatin1Char('"'));
            m_lister->runCommand(QStringLiteral("Go TABGROUPLOAD=\"%1\"").arg(n));
        }
    });
    connect(update, &QPushButton::clicked, this, [this, topOf]() {
        if (QTreeWidgetItem *it = topOf(m_groupList->currentItem()); it && m_lister) {
            QString n = it->data(0, Qt::UserRole).toString();
            n.remove(QLatin1Char('"'));
            m_lister->runCommand(QStringLiteral("Go TABGROUPSAVE=\"%1\"").arg(n));
            it->setText(1, QStringLiteral("updated"));
        }
    });
    return page;
}

QCheckBox *PreferencesDialog::viewerBox(const QString &key) const
{
    return m_viewerBoxes.value(key);
}

QCheckBox *PreferencesDialog::generalBox(const QString &key) const
{
    return m_general.value(key);
}

QWidget *PreferencesDialog::buildFileTypes()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    auto *intro = new QLabel(QStringLiteral("Applications DirDuo uses for these types of file, instead of the desktop's choice. "
                                            "Set one with right-click > Open With > Other Application... and \"Always use\"."),
                             page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_fileTypes = new QTableWidget(0, 3, page);
    m_fileTypes->setHorizontalHeaderLabels({QStringLiteral("Type"), QStringLiteral("Kind"), QStringLiteral("Opens with")});
    m_fileTypes->horizontalHeader()->setStretchLastSection(true);
    m_fileTypes->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_fileTypes->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_fileTypes->verticalHeader()->hide();
    layout->addWidget(m_fileTypes, 1);
    auto *remove = new QPushButton(QStringLiteral("Use the Desktop's Choice"), page);
    remove->setToolTip(QStringLiteral("Forget DirDuo's application for the selected types"));
    connect(remove, &QPushButton::clicked, this, [this]() {
        QSet<int> rows;
        for (QTableWidgetItem *it : m_fileTypes->selectedItems())
            rows.insert(it->row());
        for (int r : rows)
            Apps::setDefaultFor(m_fileTypes->item(r, 0)->text(), QString());
        refreshFileTypes();
    });
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(remove);
    layout->addLayout(row);
    refreshFileTypes();
    return page;
}

void PreferencesDialog::refreshFileTypes()
{
    const QMap<QString, QString> defaults = Apps::defaults();
    m_fileTypes->setRowCount(0);
    static const QMimeDatabase db;
    for (auto it = defaults.cbegin(); it != defaults.cend(); ++it) {
        const int r = m_fileTypes->rowCount();
        m_fileTypes->insertRow(r);
        const AppInfo app = Apps::find(it.value());
        m_fileTypes->setItem(r, 0, new QTableWidgetItem(it.key()));
        m_fileTypes->setItem(r, 1, new QTableWidgetItem(db.mimeTypeForName(it.key()).comment()));
        m_fileTypes->setItem(r, 2, new QTableWidgetItem(app.isValid() ? app.name : it.value() + QStringLiteral(" (not installed)")));
    }
    m_fileTypes->resizeColumnsToContents();
}

QWidget *PreferencesDialog::buildScripts()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    ScriptHost &host = m_app.scripts();
    auto *intro = new QLabel(QStringLiteral("JavaScript files in %1. Each can add commands (for buttons, menus and the console), "
                                            "columns (right-click a column header > Scripts) and handlers for events. "
                                            "Saving a script loads it again at once.")
                                 .arg(host.folder().toHtmlEscaped()),
                             page);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_scriptList = new QTreeWidget(page);
    m_scriptList->setObjectName(QStringLiteral("script-list"));
    m_scriptList->setRootIsDecorated(false);
    m_scriptList->setHeaderLabels({QStringLiteral("Script"), QStringLiteral("Version"), QStringLiteral("Adds"), QStringLiteral("Status")});
    m_scriptList->header()->resizeSection(0, 180);
    m_scriptList->header()->resizeSection(2, 260);
    layout->addWidget(m_scriptList, 1);
    auto *detail = new QLabel(page);
    detail->setWordWrap(true);
    layout->addWidget(detail);
    auto *row = new QHBoxLayout;
    auto *newBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-new")), QStringLiteral("New Script..."), page);
    auto *editBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("document-edit")), QStringLiteral("Edit"), page);
    auto *examples = new QPushButton(QStringLiteral("Add Examples"), page);
    auto *folderBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("folder-open")), QStringLiteral("Open Folder"), page);
    auto *reloadBtn = new QPushButton(QIcon::fromTheme(QStringLiteral("view-refresh")), QStringLiteral("Reload"), page);
    for (QPushButton *b : {newBtn, editBtn, examples, folderBtn, reloadBtn})
        row->addWidget(b);
    row->addStretch(1);
    layout->addLayout(row);
    layout->addWidget(new QLabel(QStringLiteral("Log (dirduo.log and errors):"), page));
    auto *log = new QPlainTextEdit(page);
    log->setObjectName(QStringLiteral("script-log"));
    log->setReadOnly(true);
    log->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    log->setMaximumHeight(fontMetrics().height() * 7);
    log->setPlainText(host.log().join(QLatin1Char('\n')));
    layout->addWidget(log);

    auto fill = [this, &host, editBtn]() {
        const QSignalBlocker block(m_scriptList);
        const QString keep = m_scriptList->currentItem() ? m_scriptList->currentItem()->data(0, Qt::UserRole).toString() : QString();
        m_scriptList->clear();
        for (const ScriptInfo &i : host.scripts()) {
            QStringList adds;
            if (!i.commands.isEmpty())
                adds << QStringLiteral("commands: ") + i.commands.join(QStringLiteral(", "));
            if (!i.columns.isEmpty())
                adds << QStringLiteral("columns: ") + i.columns.join(QStringLiteral(", "));
            if (!i.events.isEmpty())
                adds << QStringLiteral("events: ") + i.events.join(QStringLiteral(", "));
            auto *it = new QTreeWidgetItem(m_scriptList, {i.name, i.version, adds.join(QStringLiteral("; ")),
                                                          !i.enabled ? QStringLiteral("Off") : i.error.isEmpty() ? QStringLiteral("OK") : i.error});
            it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
            it->setCheckState(0, i.enabled ? Qt::Checked : Qt::Unchecked);
            it->setData(0, Qt::UserRole, i.id);
            it->setData(0, Qt::UserRole + 1, i.file);
            it->setToolTip(0, i.file);
            it->setToolTip(3, i.error);
            if (!i.error.isEmpty())
                it->setForeground(3, QColor(0xd0, 0x40, 0x30));
            if (i.id == keep)
                m_scriptList->setCurrentItem(it);
        }
        editBtn->setEnabled(m_scriptList->currentItem());
    };
    fill();
    connect(&host, &ScriptHost::scriptsChanged, page, fill);
    connect(&host, &ScriptHost::logged, log, [log](const QString &line) { log->appendPlainText(line); });
    connect(m_scriptList, &QTreeWidget::itemChanged, this, [&host](QTreeWidgetItem *it, int col) {
        if (col == 0)
            host.setEnabled(it->data(0, Qt::UserRole).toString(), it->checkState(0) == Qt::Checked);
    });
    connect(m_scriptList, &QTreeWidget::currentItemChanged, this, [&host, detail, editBtn](QTreeWidgetItem *it) {
        editBtn->setEnabled(it);
        if (!it) {
            detail->clear();
            return;
        }
        for (const ScriptInfo &i : host.scripts()) {
            if (i.id == it->data(0, Qt::UserRole).toString())
                detail->setText(i.description.isEmpty() ? i.file : i.description + (i.author.isEmpty() ? QString() : QStringLiteral(" (") + i.author + QLatin1Char(')')));
        }
    });
    auto openFile = [](const QString &path) { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); };
    connect(newBtn, &QPushButton::clicked, this, [this, &host, openFile]() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("New Script"), QStringLiteral("Name:"), QLineEdit::Normal,
                                                   QStringLiteral("My Tools"), &ok);
        if (!ok)
            return;
        QString err;
        const QString path = host.newScript(name, &err);
        if (path.isEmpty())
            QMessageBox::warning(this, QStringLiteral("New Script"), err);
        else
            openFile(path);
    });
    connect(editBtn, &QPushButton::clicked, this, [this, openFile]() {
        if (QTreeWidgetItem *it = m_scriptList->currentItem())
            openFile(it->data(0, Qt::UserRole + 1).toString());
    });
    connect(m_scriptList, &QTreeWidget::itemDoubleClicked, this, [openFile](QTreeWidgetItem *it) {
        openFile(it->data(0, Qt::UserRole + 1).toString());
    });
    connect(examples, &QPushButton::clicked, this, [&host]() {
        host.addExamples();
        host.reload();
    });
    connect(folderBtn, &QPushButton::clicked, this, [this]() {
        if (m_lister)
            m_lister->runCommand(QStringLiteral("Script FOLDER"));
    });
    connect(reloadBtn, &QPushButton::clicked, &host, &ScriptHost::reload);
    return page;
}

QWidget *PreferencesDialog::buildStatusBar()
{
    auto *page = new QWidget;
    auto *layout = new QVBoxLayout(page);
    const Prefs::Values &v = Prefs::instance()->values();
    auto box = [&](const QString &key, const QString &text, bool on, QLayout *into, QWidget *parent) {
        auto *b = new QCheckBox(text, parent);
        b->setChecked(on);
        into->addWidget(b);
        m_general.insert(key, b);
        return b;
    };
    auto *simple = new QGroupBox(QStringLiteral("Simple"), page);
    auto *sl = new QVBoxLayout(simple);
    box(QStringLiteral("counts"), QStringLiteral("Folders and files in the list"), v.statusCounts, sl, simple);
    box(QStringLiteral("selection"), QStringLiteral("What is selected, and its size"), v.statusSelection, sl, simple);
    box(QStringLiteral("free"), QStringLiteral("Free space on the drive or card"), v.statusFree, sl, simple);
    layout->addWidget(simple);

    QCheckBox *custom = box(QStringLiteral("custom"), QStringLiteral("Use my own definition, with codes"),
                            !v.statusDefinition.trimmed().isEmpty(), layout, page);
    auto *defBox = new QGroupBox(QStringLiteral("Definition"), page);
    auto *dl = new QVBoxLayout(defBox);
    auto *row = new QHBoxLayout;
    auto *codesBtn = new QToolButton(defBox);
    codesBtn->setText(QStringLiteral("Codes"));
    codesBtn->setPopupMode(QToolButton::InstantPopup);
    auto *codesMenu = new QMenu(codesBtn);
    QHash<QString, QMenu *> groups;
    m_statusDef = new QPlainTextEdit(defBox);
    m_statusDef->setObjectName(QStringLiteral("status-definition"));
    m_statusDef->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    m_statusDef->setPlainText(v.effectiveStatusDefinition());
    m_statusDef->setFixedHeight(fontMetrics().height() * 6);
    for (const StatusFormat::CodeInfo &c : StatusFormat::codes()) {
        QMenu *&g = groups[c.group];
        if (!g)
            g = codesMenu->addMenu(c.group);
        QAction *a = g->addAction(QStringLiteral("%1   %2").arg(c.text, c.code));
        const QString code = c.code;
        connect(a, &QAction::triggered, this, [this, code]() {
            QTextCursor cur = m_statusDef->textCursor();
            // Paired tags go around what is selected, the cursor between them.
            const int mid = code.indexOf(QStringLiteral("></"));
            if (code.startsWith(QLatin1Char('<')) && mid > 0) {
                const QString sel = cur.selectedText();
                cur.insertText(code.left(mid + 1) + sel + code.mid(mid + 1));
                if (sel.isEmpty())
                    cur.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, int(code.size() - mid - 1));
            } else if (code == QLatin1String("[ ]")) {
                const QString sel = cur.selectedText();
                cur.insertText(QLatin1Char('[') + sel + QLatin1Char(']'));
                if (sel.isEmpty())
                    cur.movePosition(QTextCursor::Left);
            } else {
                cur.insertText(code);
            }
            m_statusDef->setTextCursor(cur);
            m_statusDef->setFocus();
        });
    }
    codesBtn->setMenu(codesMenu);
    auto *samplesBtn = new QToolButton(defBox);
    samplesBtn->setText(QStringLiteral("Samples"));
    samplesBtn->setPopupMode(QToolButton::InstantPopup);
    auto *samplesMenu = new QMenu(samplesBtn);
    for (const auto &smp : StatusFormat::samples()) {
        const QString text = smp.second;
        connect(samplesMenu->addAction(smp.first), &QAction::triggered, this, [this, text]() { m_statusDef->setPlainText(text); });
    }
    samplesBtn->setMenu(samplesMenu);
    row->addWidget(codesBtn);
    row->addWidget(samplesBtn);
    row->addStretch(1);
    dl->addLayout(row);
    dl->addWidget(m_statusDef);
    auto *help = new QLabel(QStringLiteral("{right} moves what follows to the right; [ ] shows a part only when a code in it has a value; "
                                           "<b>, <i>, <u> and <#rrggbb> style text. Preview, for the list you are in:"),
                            defBox);
    help->setTextFormat(Qt::PlainText);
    help->setWordWrap(true);
    dl->addWidget(help);
    m_statusPreview = new StatusLine(defBox);
    m_statusPreview->setObjectName(QStringLiteral("status-preview"));
    m_statusPreview->setAutoFillBackground(true);
    m_statusPreview->setBackgroundRole(QPalette::Base);
    dl->addWidget(m_statusPreview);
    m_statusError = new QLabel(defBox);
    m_statusError->setStyleSheet(QStringLiteral("color: #d04030"));
    dl->addWidget(m_statusError);
    layout->addWidget(defBox);
    layout->addStretch(1);

    auto refresh = [this, custom, simple, defBox]() {
        simple->setEnabled(!custom->isChecked());
        m_statusDef->setReadOnly(!custom->isChecked());
        if (!custom->isChecked()) {
            const QSignalBlocker b(m_statusDef);
            m_statusDef->setPlainText(StatusFormat::defaultDefinition(m_general.value(QStringLiteral("counts"))->isChecked(),
                                                                      m_general.value(QStringLiteral("selection"))->isChecked(),
                                                                      m_general.value(QStringLiteral("free"))->isChecked()));
        }
        const QString def = m_statusDef->toPlainText();
        QString err;
        m_statusError->setText(StatusFormat::check(def, &err) ? QString() : err);
        QHash<QString, QString> values;
        if (m_lister) {
            if (FolderTab *t = m_lister->sourceTabForTesting())
                values = t->statusValues(true);
        }
        values.insert(QStringLiteral("role"), QStringLiteral("SOURCE"));
        values.insert(QStringLiteral("tabs"), QStringLiteral("1"));
        m_statusPreview->setSegments(StatusFormat::render(def, values));
        Q_UNUSED(defBox);
    };
    connect(custom, &QCheckBox::toggled, this, refresh);
    for (const char *k : {"counts", "selection", "free"})
        connect(m_general.value(QLatin1String(k)), &QCheckBox::toggled, this, refresh);
    connect(m_statusDef, &QPlainTextEdit::textChanged, this, refresh);
    refresh();
    return page;
}

void PreferencesDialog::addPage(const QString &key, const QString &title, const QString &icon, QWidget *page, const QStringList &keywords)
{
    auto *scroll = new QScrollArea(m_stack);
    scroll->setWidget(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    const int index = m_stack->addWidget(scroll);
    auto *item = new QListWidgetItem(QIcon::fromTheme(icon), title, m_nav);
    item->setData(Qt::UserRole, index);
    item->setData(Qt::UserRole + 1, key);
    // Everything a page says counts for the search, as well as its keywords.
    QStringList words = keywords;
    words << title;
    for (QLabel *l : page->findChildren<QLabel *>())
        words << l->text();
    for (QAbstractButton *b : page->findChildren<QAbstractButton *>())
        words << b->text();
    for (QGroupBox *g : page->findChildren<QGroupBox *>())
        words << g->title();
    for (QTableWidget *t : page->findChildren<QTableWidget *>()) {
        for (int c = 0; c < t->columnCount(); ++c)
            if (t->horizontalHeaderItem(c))
                words << t->horizontalHeaderItem(c)->text();
    }
    m_keys << key;
    m_keywords << words;
}

bool PreferencesDialog::setPage(const QString &name)
{
    for (int r = 0; r < m_nav->count(); ++r) {
        if (m_nav->item(r)->data(Qt::UserRole + 1).toString().compare(name, Qt::CaseInsensitive) == 0) {
            m_nav->item(r)->setHidden(false);
            m_nav->setCurrentRow(r);
            return true;
        }
    }
    return false;
}

QString PreferencesDialog::currentPage() const
{
    QListWidgetItem *i = m_nav->currentItem();
    return i ? i->data(Qt::UserRole + 1).toString() : QString();
}

QStringList PreferencesDialog::search(const QString &text)
{
    const QString t = text.trimmed();
    QStringList shown;
    for (int r = 0; r < m_nav->count(); ++r) {
        const int idx = m_keys.indexOf(m_nav->item(r)->data(Qt::UserRole + 1).toString());
        bool match = t.isEmpty();
        for (const QString &w : m_keywords.at(idx))
            match = match || w.contains(t, Qt::CaseInsensitive);
        m_nav->item(r)->setHidden(!match);
        if (match)
            shown << m_keys.at(idx);
    }
    // Land on the first page that matches.
    if (!shown.isEmpty() && (m_nav->currentItem() == nullptr || m_nav->currentItem()->isHidden()))
        setPage(shown.first());
    return shown;
}

// ---------------------------------------------------------------- appearance

QWidget *PreferencesDialog::buildAppearance()
{
    auto *w = new QWidget;
    auto *form = new QFormLayout(w);
    m_themeBox = new QComboBox(w);
    m_sizeBox = new QComboBox(w);
    m_sizeBox->addItems({QStringLiteral("Normal"), QStringLiteral("Large"), QStringLiteral("Larger")});
    m_sizeBox->setCurrentIndex(Style::instance()->sizeLevel());
    form->addRow(QStringLiteral("Theme:"), m_themeBox);
    form->addRow(QStringLiteral("Text and icon size:"), m_sizeBox);
    auto *hint = new QLabel(QStringLiteral("Changes show at once; Cancel puts back what you had. The Theme Editor page "
                                           "changes any color and saves the result as your own theme."),
                            w);
    hint->setWordWrap(true);
    hint->setEnabled(false);
    form->addRow(hint);
    auto *edit = new QPushButton(QIcon::fromTheme(QStringLiteral("color-management")), QStringLiteral("Edit This Theme..."), w);
    form->addRow(QString(), edit);
    connect(edit, &QPushButton::clicked, this, [this]() { setPage(QStringLiteral("theme")); });
    connect(m_themeBox, &QComboBox::currentTextChanged, this, [this](const QString &name) {
        if (m_loading || name.isEmpty())
            return;
        Style::instance()->setTheme(name);
        loadThemeIntoEditor(Style::instance()->theme());
    });
    connect(m_sizeBox, &QComboBox::currentIndexChanged, this, [](int level) { Style::instance()->setSizeLevel(level); });
    return w;
}

// ---------------------------------------------------------------- theme editor

QWidget *PreferencesDialog::buildTheme()
{
    auto *w = new QWidget;
    auto *layout = new QVBoxLayout(w);
    auto *top = new QFormLayout;
    m_startFrom = new QComboBox(w);
    m_themeName = new QLineEdit(w);
    top->addRow(QStringLiteral("Start from:"), m_startFrom);
    top->addRow(QStringLiteral("Name:"), m_themeName);
    m_themeDark = new QCheckBox(QStringLiteral("Dark theme (uses the dark icon set)"), w);
    top->addRow(QString(), m_themeDark);
    layout->addLayout(top);

    auto *colors = new QGroupBox(QStringLiteral("Colors"), w);
    auto *grid = new QGridLayout(colors);
    int row = 0;
    int col = 0;
    for (const ColorSlot &s : colorSlots()) {
        auto *b = new ColorButton(colors);
        m_colorButtons.insert(QLatin1String(s.key), b);
        grid->addWidget(new QLabel(QString::fromUtf8(s.label), colors), row, col * 2);
        grid->addWidget(b, row, col * 2 + 1);
        const QString key = QLatin1String(s.key);
        connect(b, &ColorButton::colorChanged, this, [this, key](const QColor &c) { setThemeColor(key, c); });
        if (++col == 2) {
            col = 0;
            ++row;
        }
    }
    layout->addWidget(colors);

    auto *types = new QGroupBox(QStringLiteral("File types"), w);
    auto *tg = new QGridLayout(types);
    m_colorTypes = new QCheckBox(QStringLiteral("Color names by file type"), types);
    tg->addWidget(m_colorTypes, 0, 0, 1, 4);
    row = 1;
    col = 0;
    for (FileKind::Category c : typeCategories()) {
        auto *b = new ColorButton(types);
        const QString key = QStringLiteral("type:") + FileKind::categoryKey(c);
        m_colorButtons.insert(key, b);
        tg->addWidget(new QLabel(FileKind::categoryName(c), types), row, col * 2);
        tg->addWidget(b, row, col * 2 + 1);
        connect(b, &ColorButton::colorChanged, this, [this, key](const QColor &cc) { setThemeColor(key, cc); });
        if (++col == 2) {
            col = 0;
            ++row;
        }
    }
    layout->addWidget(types);

    auto *actions = new QHBoxLayout;
    auto *save = new QPushButton(QIcon::fromTheme(QStringLiteral("document-save")), QStringLiteral("Save Theme"), w);
    auto *del = new QPushButton(QIcon::fromTheme(QStringLiteral("edit-delete")), QStringLiteral("Delete Theme"), w);
    m_themeStatus = new QLabel(w);
    actions->addWidget(save);
    actions->addWidget(del);
    actions->addWidget(m_themeStatus, 1);
    layout->addLayout(actions);
    auto *where = new QLabel(QStringLiteral("Saved themes are .ini files in %1, to copy and share.").arg(Style::instance()->themesDir()), w);
    where->setWordWrap(true);
    where->setEnabled(false);
    layout->addWidget(where);
    layout->addStretch(1);

    connect(m_startFrom, &QComboBox::activated, this, [this](int) {
        Theme t;
        const QString name = m_startFrom->currentText();
        if (Theme::builtinNames().contains(name))
            t = Theme::builtin(name);
        else if (!Theme::load(QDir(Style::instance()->themesDir()).filePath(safeFileName(name) + QStringLiteral(".ini")), t))
            return;
        if (t.followSystem) {
            // The desktop's colors as they are now, to change from there.
            const bool keep = m_loading;
            m_loading = true;
            Style::instance()->setTheme(t);
            t = Style::instance()->theme();
            t.followSystem = false;
            m_loading = keep;
        }
        if (Theme::builtinNames().contains(name))
            t.name = name + QStringLiteral(" (mine)");
        loadThemeIntoEditor(t);
        m_themeDirty = true;
        previewTheme();
    });
    connect(m_themeName, &QLineEdit::textEdited, this, [this](const QString &n) {
        m_theme.name = n.trimmed();
        m_themeDirty = true;
    });
    connect(m_themeDark, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        m_theme.dark = on;
        m_themeDirty = true;
        previewTheme();
    });
    connect(m_colorTypes, &QCheckBox::toggled, this, [this](bool on) {
        if (m_loading)
            return;
        m_theme.colorFileTypes = on;
        m_themeDirty = true;
        previewTheme();
    });
    connect(save, &QPushButton::clicked, this, [this]() {
        QString err;
        if (!saveTheme(&err))
            m_themeStatus->setText(err);
    });
    connect(del, &QPushButton::clicked, this, [this]() {
        const QString file = QDir(Style::instance()->themesDir()).filePath(safeFileName(m_theme.name) + QStringLiteral(".ini"));
        if (!QFile::exists(file)) {
            m_themeStatus->setText(QStringLiteral("Only your own saved themes can be deleted."));
            return;
        }
        if (QMessageBox::question(this, QStringLiteral("Delete Theme"), QStringLiteral("Delete the theme \"%1\"?").arg(m_theme.name))
            != QMessageBox::Yes)
            return;
        QFile::remove(file);
        Style::instance()->setTheme(QStringLiteral("Follow Desktop"));
        refreshThemeLists(QStringLiteral("Follow Desktop"));
        loadThemeIntoEditor(Style::instance()->theme());
        m_themeDirty = false;
        m_themeStatus->setText(QStringLiteral("Deleted."));
    });

    refreshThemeLists(Style::instance()->theme().name);
    loadThemeIntoEditor(Style::instance()->theme());
    return w;
}

void PreferencesDialog::refreshThemeLists(const QString &select)
{
    const bool keep = m_loading;
    m_loading = true;
    const QStringList names = Style::instance()->themeNames();
    for (QComboBox *box : {m_themeBox, m_startFrom}) {
        if (!box)
            continue;
        box->clear();
        box->addItems(names);
        box->setCurrentText(select);
    }
    m_loading = keep;
}

void PreferencesDialog::loadThemeIntoEditor(const Theme &t)
{
    const bool keep = m_loading;
    m_loading = true;
    m_theme = t;
    if (m_themeName)
        m_themeName->setText(t.name);
    if (m_themeDark)
        m_themeDark->setChecked(t.dark);
    if (m_colorTypes)
        m_colorTypes->setChecked(t.colorFileTypes);
    for (const ColorSlot &s : colorSlots()) {
        if (ColorButton *b = m_colorButtons.value(QLatin1String(s.key)))
            b->setColor(t.*(s.member));
    }
    for (FileKind::Category c : typeCategories()) {
        if (ColorButton *b = m_colorButtons.value(QStringLiteral("type:") + FileKind::categoryKey(c)))
            b->setColor(t.typeColor(c));
    }
    m_loading = keep;
}

void PreferencesDialog::setThemeColor(const QString &key, const QColor &c)
{
    if (key.startsWith(QLatin1String("type:"))) {
        FileKind::Category cat;
        if (FileKind::categoryFromKey(key.mid(5), cat))
            m_theme.types.insert(cat, c);
    } else {
        for (const ColorSlot &s : colorSlots()) {
            if (key == QLatin1String(s.key))
                m_theme.*(s.member) = c;
        }
    }
    if (ColorButton *b = m_colorButtons.value(key))
        b->setColor(c);
    // An edited "Follow Desktop" is a theme of its own from now on.
    m_theme.followSystem = false;
    m_themeDirty = true;
    previewTheme();
}

void PreferencesDialog::setThemeName(const QString &name)
{
    m_themeName->setText(name);
    m_theme.name = name.trimmed();
    m_themeDirty = true;
}

void PreferencesDialog::previewTheme()
{
    if (m_loading)
        return;
    Style::instance()->setTheme(m_theme);
    if (m_themeStatus)
        m_themeStatus->setText(QStringLiteral("Not saved yet."));
}

bool PreferencesDialog::saveTheme(QString *error)
{
    QString name = m_theme.name.trimmed();
    if (name.isEmpty()) {
        if (error)
            *error = QStringLiteral("Give the theme a name first.");
        return false;
    }
    if (Theme::builtinNames().contains(name)) {
        name += QStringLiteral(" (mine)"); // the built-ins stay as they are
        m_theme.name = name;
        m_themeName->setText(name);
    }
    QDir().mkpath(Style::instance()->themesDir());
    const QString file = QDir(Style::instance()->themesDir()).filePath(safeFileName(name) + QStringLiteral(".ini"));
    m_theme.followSystem = false;
    if (!m_theme.save(file)) {
        if (error)
            *error = QStringLiteral("Could not write %1.").arg(file);
        return false;
    }
    m_themeDirty = false;
    Style::instance()->setTheme(name);
    refreshThemeLists(name);
    m_app.settings().setValue(QStringLiteral("Appearance/theme"), name);
    if (m_themeStatus)
        m_themeStatus->setText(QStringLiteral("Saved as %1.").arg(QFileInfo(file).fileName()));
    return true;
}

// ---------------------------------------------------------------- labels

QWidget *PreferencesDialog::buildLabels()
{
    auto *w = new QWidget;
    auto *layout = new QVBoxLayout(w);
    auto *intro = new QLabel(QStringLiteral("Names matching a pattern get their own color and style, in every list. "
                                            "The first matching row wins. Patterns use wildcards; ; between several."),
                             w);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_labels = new QTableWidget(0, 6, w);
    m_labels->setHorizontalHeaderLabels({QStringLiteral("Pattern"), QStringLiteral("Applies to"), QStringLiteral("Text"),
                                         QStringLiteral("Background"), QStringLiteral("Bold"), QStringLiteral("Italic")});
    QHeaderView *lh = m_labels->horizontalHeader();
    lh->setStretchLastSection(false);
    lh->setSectionResizeMode(QHeaderView::Fixed);
    lh->setSectionResizeMode(0, QHeaderView::Stretch);
    const int unit = m_labels->fontMetrics().averageCharWidth();
    lh->resizeSection(1, unit * 14);
    lh->resizeSection(2, unit * 9);
    lh->resizeSection(3, unit * 12);
    lh->resizeSection(4, unit * 7);
    lh->resizeSection(5, unit * 7);
    m_labels->verticalHeader()->hide();
    m_labels->setSelectionBehavior(QAbstractItemView::SelectRows);
    layout->addWidget(m_labels, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add"), w);
    auto *remove = new QPushButton(QStringLiteral("Remove"), w);
    auto *up = new QPushButton(QStringLiteral("Up"), w);
    auto *down = new QPushButton(QStringLiteral("Down"), w);
    for (QPushButton *b : {add, remove, up, down})
        row->addWidget(b);
    row->addStretch(1);
    layout->addLayout(row);

    for (const LabelRule &r : Style::instance()->labels().rules())
        addLabelRule(r);
    connect(add, &QPushButton::clicked, this, [this]() {
        LabelRule r;
        r.pattern = QStringLiteral("*.d64;*.adf");
        r.color = QColor::fromRgb(0xe0, 0x7a, 0x10);
        addLabelRule(r);
        m_labels->setCurrentCell(m_labels->rowCount() - 1, 0);
        m_labels->editItem(m_labels->item(m_labels->rowCount() - 1, 0));
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_labels->currentRow() >= 0)
            m_labels->removeRow(m_labels->currentRow());
    });
    auto move = [this](int delta) {
        const int r = m_labels->currentRow();
        const int to = r + delta;
        if (r < 0 || to < 0 || to >= m_labels->rowCount())
            return;
        QList<LabelRule> rules = labelRules();
        rules.swapItemsAt(r, to);
        m_labels->setRowCount(0);
        for (const LabelRule &x : rules)
            addLabelRule(x);
        m_labels->setCurrentCell(to, 0);
    };
    connect(up, &QPushButton::clicked, this, [move]() { move(-1); });
    connect(down, &QPushButton::clicked, this, [move]() { move(1); });
    return w;
}

void PreferencesDialog::addLabelRule(const LabelRule &r)
{
    const int row = m_labels->rowCount();
    m_labels->insertRow(row);
    fillLabelRow(row, r);
}

void PreferencesDialog::fillLabelRow(int row, const LabelRule &r)
{
    m_labels->setItem(row, 0, new QTableWidgetItem(r.pattern));
    auto *applies = new QComboBox(m_labels);
    applies->addItems({QStringLiteral("Files"), QStringLiteral("Folders"), QStringLiteral("Both")});
    applies->setCurrentIndex(int(r.applies));
    m_labels->setCellWidget(row, 1, applies);
    auto *fg = new ColorButton(m_labels);
    fg->setAllowNone(true);
    fg->setColor(r.color);
    m_labels->setCellWidget(row, 2, fg);
    auto *bg = new ColorButton(m_labels);
    bg->setAllowNone(true);
    bg->setColor(r.background);
    m_labels->setCellWidget(row, 3, bg);
    for (int c : {4, 5}) {
        auto *box = new QCheckBox(m_labels);
        box->setChecked(c == 4 ? r.bold : r.italic);
        m_labels->setCellWidget(row, c, box);
    }
}

QList<LabelRule> PreferencesDialog::labelRules() const
{
    QList<LabelRule> out;
    for (int r = 0; r < m_labels->rowCount(); ++r) {
        LabelRule rule;
        rule.pattern = m_labels->item(r, 0) ? m_labels->item(r, 0)->text().trimmed() : QString();
        if (rule.pattern.isEmpty())
            continue;
        rule.applies = static_cast<LabelRule::Applies>(qobject_cast<QComboBox *>(m_labels->cellWidget(r, 1))->currentIndex());
        rule.color = qobject_cast<ColorButton *>(m_labels->cellWidget(r, 2))->color();
        rule.background = qobject_cast<ColorButton *>(m_labels->cellWidget(r, 3))->color();
        rule.bold = qobject_cast<QCheckBox *>(m_labels->cellWidget(r, 4))->isChecked();
        rule.italic = qobject_cast<QCheckBox *>(m_labels->cellWidget(r, 5))->isChecked();
        out << rule;
    }
    return out;
}

// ---------------------------------------------------------------- aliases

QWidget *PreferencesDialog::buildAliases()
{
    auto *w = new QWidget;
    auto *layout = new QVBoxLayout(w);
    auto *intro = new QLabel(QStringLiteral("Type /name anywhere a folder is asked for: the address bar, buttons, commands. "
                                            "An alias can point at a server (sftp://...) too."),
                             w);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_aliases = new QTableWidget(0, 2, w);
    m_aliases->setHorizontalHeaderLabels({QStringLiteral("Alias"), QStringLiteral("Folder")});
    m_aliases->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_aliases->verticalHeader()->hide();
    AliasManager &aliases = m_app.aliases();
    QStringList builtIns;
    for (const QString &name : aliases.names()) {
        if (aliases.isUserAlias(name)) {
            const int r = addAliasRow();
            setAliasRow(r, name, aliases.aliasPath(name));
        } else {
            builtIns << QStringLiteral("/%1 = %2").arg(name, aliases.aliasPath(name));
        }
    }
    layout->addWidget(m_aliases, 1);
    auto *row = new QHBoxLayout;
    auto *add = new QPushButton(QStringLiteral("Add"), w);
    auto *browse = new QPushButton(QStringLiteral("Choose Folder..."), w);
    auto *remove = new QPushButton(QStringLiteral("Remove"), w);
    for (QPushButton *b : {add, browse, remove})
        row->addWidget(b);
    row->addStretch(1);
    layout->addLayout(row);
    auto *builtin = new QLabel(QStringLiteral("Built in: ") + builtIns.join(QStringLiteral(", ")), w);
    builtin->setWordWrap(true);
    builtin->setEnabled(false);
    builtin->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(builtin);

    connect(add, &QPushButton::clicked, this, [this]() {
        const int r = addAliasRow();
        setAliasRow(r, QString(), m_lister ? m_lister->sourcePath() : QDir::homePath());
        m_aliases->setCurrentCell(r, 0);
        m_aliases->editItem(m_aliases->item(r, 0));
    });
    connect(browse, &QPushButton::clicked, this, [this]() {
        const int r = m_aliases->currentRow();
        if (r < 0)
            return;
        const QString d = QFileDialog::getExistingDirectory(this, QStringLiteral("Alias Folder"), m_aliases->item(r, 1)->text());
        if (!d.isEmpty())
            m_aliases->item(r, 1)->setText(d);
    });
    connect(remove, &QPushButton::clicked, this, [this]() {
        if (m_aliases->currentRow() >= 0)
            m_aliases->removeRow(m_aliases->currentRow());
    });
    return w;
}

int PreferencesDialog::addAliasRow()
{
    const int r = m_aliases->rowCount();
    m_aliases->insertRow(r);
    m_aliases->setItem(r, 0, new QTableWidgetItem);
    m_aliases->setItem(r, 1, new QTableWidgetItem);
    return r;
}

void PreferencesDialog::setAliasRow(int row, const QString &name, const QString &path)
{
    m_aliases->item(row, 0)->setText(name);
    m_aliases->item(row, 1)->setText(path);
}

// ---------------------------------------------------------------- folder formats

QWidget *PreferencesDialog::buildFormats()
{
    auto *w = new QWidget;
    auto *layout = new QVBoxLayout(w);
    auto *intro = new QLabel(QStringLiteral("Folders with a format of their own open with their saved columns, sort, grouping and view. "
                                            "Save one from View > Folder Format while in the folder."),
                             w);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    m_formats = new QTreeWidget(w);
    m_formats->setHeaderLabels({QStringLiteral("Folder"), QStringLiteral("Subfolders too"), QStringLiteral("View"), QStringLiteral("Group by")});
    m_formats->setRootIsDecorated(false);
    m_formats->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    layout->addWidget(m_formats, 1);
    auto *row = new QHBoxLayout;
    auto *remove = new QPushButton(QStringLiteral("Remove"), w);
    row->addWidget(remove);
    row->addStretch(1);
    layout->addLayout(row);
    m_defaultFormat = new QLabel(w);
    auto *clearDefault = new QPushButton(QStringLiteral("Clear the Default Format"), w);
    auto *dl = new QHBoxLayout;
    dl->addWidget(m_defaultFormat, 1);
    dl->addWidget(clearDefault);
    layout->addLayout(dl);
    m_autoFormat = new QCheckBox(QStringLiteral("Folders that are mostly pictures open as icons"), w);
    m_autoFormat->setChecked(FolderFormats::instance().autoSwitch());
    layout->addWidget(m_autoFormat);

    connect(remove, &QPushButton::clicked, this, [this]() {
        if (QTreeWidgetItem *i = m_formats->currentItem()) {
            FolderFormats::instance().remove(i->text(0));
            refreshFormats();
        }
    });
    connect(clearDefault, &QPushButton::clicked, this, [this]() {
        FolderFormats::instance().clearDefault();
        refreshFormats();
    });
    refreshFormats();
    return w;
}

void PreferencesDialog::refreshFormats()
{
    m_formats->clear();
    for (const FolderFormat &f : FolderFormats::instance().all())
        new QTreeWidgetItem(m_formats, {f.path, f.subfolders ? QStringLiteral("yes") : QString(), f.view, f.groupBy});
    FolderFormat d;
    m_defaultFormat->setText(FolderFormats::instance().defaultFormat(d)
                                 ? QStringLiteral("Default for other folders: %1 view, grouped by %2.").arg(d.view, d.groupBy)
                                 : QStringLiteral("No default format: other folders keep the look they had."));
}

// ---------------------------------------------------------------- toolbars

QWidget *PreferencesDialog::buildToolbars()
{
    auto *w = new QWidget;
    auto *layout = new QVBoxLayout(w);
    auto *intro = new QLabel(QStringLiteral("Toolbars are edited in place: customize mode lets you drag buttons around, "
                                            "edit them, make new toolbars and add ready-made buttons."),
                             w);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    auto *customize = new QPushButton(QIcon::fromTheme(QStringLiteral("configure-toolbars")), QStringLiteral("Customize Toolbars..."), w);
    auto *reset = new QPushButton(QStringLiteral("Reset the Standard Toolbar"), w);
    layout->addWidget(customize);
    layout->addWidget(reset);
    auto *where = new QLabel(QStringLiteral("Toolbar files: %1").arg(Paths::configFile(QStringLiteral("toolbars"))), w);
    where->setEnabled(false);
    where->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(where);
    layout->addStretch(1);
    connect(customize, &QPushButton::clicked, this, [this]() {
        if (m_lister)
            m_lister->customize();
    });
    connect(reset, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QStringLiteral("Reset Toolbar"), QStringLiteral("Put back the standard buttons?"))
            == QMessageBox::Yes)
            m_app.toolbars().resetToDefault(QLatin1String(ToolbarStore::kMainId));
    });
    return w;
}

// ---------------------------------------------------------------- apply

bool PreferencesDialog::apply(QString *error)
{
    // General.
    Prefs::Values v = Prefs::instance()->values();
    v.statusCounts = m_general.value(QStringLiteral("counts"))->isChecked();
    v.statusSelection = m_general.value(QStringLiteral("selection"))->isChecked();
    v.statusFree = m_general.value(QStringLiteral("free"))->isChecked();
    v.askOnError = m_general.value(QStringLiteral("ask"))->isChecked();
    if (m_general.value(QStringLiteral("custom"))->isChecked()) {
        QString err;
        if (!StatusFormat::check(m_statusDef->toPlainText(), &err)) {
            if (error)
                *error = QStringLiteral("Status bar: %1").arg(err);
            return false;
        }
        v.statusDefinition = m_statusDef->toPlainText().trimmed();
    } else {
        v.statusDefinition.clear();
    }
    v.junkHidden = m_general.value(QStringLiteral("junk"))->isChecked();
    v.treeLines = m_general.value(QStringLiteral("treelines"))->isChecked();
    v.treePath = m_general.value(QStringLiteral("treepath"))->isChecked();
    v.notifyJobs = m_general.value(QStringLiteral("notify"))->isChecked();
    v.notifyAfter = m_notifyAfter->value();
    const QString typed = m_terminal->currentText().trimmed();
    const int at = m_terminal->findText(typed);
    v.terminal = at >= 0 ? m_terminal->itemData(at).toString() : typed;
    // Viewer.
    Prefs::Values::Viewer &vw = v.viewer;
    vw.openPictures = m_viewerBoxes.value(QStringLiteral("pictures"))->isChecked();
    vw.openMedia = m_viewerBoxes.value(QStringLiteral("media"))->isChecked();
    vw.siblings = m_viewerBoxes.value(QStringLiteral("siblings"))->isChecked();
    vw.reuse = m_viewerBoxes.value(QStringLiteral("reuse"))->isChecked();
    vw.wrap = m_viewerBoxes.value(QStringLiteral("wrap"))->isChecked();
    vw.fullPathTitle = m_viewerBoxes.value(QStringLiteral("fullpath"))->isChecked();
    vw.autoPlay = m_viewerBoxes.value(QStringLiteral("autoplay"))->isChecked();
    vw.playNext = m_viewerBoxes.value(QStringLiteral("playnext"))->isChecked();
    vw.slideRandom = m_viewerBoxes.value(QStringLiteral("random"))->isChecked();
    vw.slideAuto = m_viewerBoxes.value(QStringLiteral("slideauto"))->isChecked();
    vw.openMarkedOnClose = m_viewerBoxes.value(QStringLiteral("openmarked"))->isChecked();
    vw.resetZoom = m_viewerZoom->currentIndex();
    vw.background = m_viewerBackground->currentIndex();
    vw.wheelSteps = m_viewerWheel->currentIndex() == 0;
    vw.slideSeconds = m_slideSeconds->value();
    vw.markCollection = m_markName->text().trimmed().isEmpty() ? Prefs::Values::Viewer().markCollection : m_markName->text().trimmed();
    Prefs::instance()->setValues(v);
    if (!m_keysPage->apply(error))
        return false;

    // Saved filters.
    QList<SavedFilters::Filter> filters;
    QStringList seen;
    for (int i = 0; i < m_filterList->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *it = m_filterList->topLevelItem(i);
        SavedFilters::Filter f;
        f.name = it->text(0).trimmed();
        f.pattern = it->text(1).trimmed();
        f.hide = it->checkState(2) == Qt::Checked;
        f.scope = it->text(3);
        if (f.name.isEmpty() || f.pattern.isEmpty())
            continue;
        if (seen.contains(f.name.toLower())) {
            if (error)
                *error = QStringLiteral("Filters: two are called \"%1\".").arg(f.name);
            return false;
        }
        seen << f.name.toLower();
        filters << f;
    }
    if (filters != SavedFilters::instance()->filters() || !QFile::exists(SavedFilters::instance()->file()))
        SavedFilters::instance()->setFilters(filters);

    // Tab groups: renamed, deleted, close-first changed.
    QList<TabGroups::Group> groups;
    for (int i = 0; i < m_groupList->topLevelItemCount(); ++i) {
        const QTreeWidgetItem *it = m_groupList->topLevelItem(i);
        TabGroups::Group g = TabGroups::instance()->group(it->data(0, Qt::UserRole).toString());
        if (g.name.isEmpty())
            continue;
        const QString newName = it->text(0).trimmed();
        if (!newName.isEmpty())
            g.name = newName;
        g.closeOthers = it->checkState(2) == Qt::Checked;
        groups << g;
    }
    if (m_groupList->topLevelItemCount() != TabGroups::instance()->groups().size() || !groups.isEmpty())
        TabGroups::instance()->setGroups(groups);

    // Theme: an edited theme is saved under its name, so it survives a restart.
    if (m_themeDirty && !saveTheme(error))
        return false;
    const QString theme = Style::instance()->theme().name;
    if (Style::instance()->themeNames().contains(theme))
        m_app.settings().setValue(QStringLiteral("Appearance/theme"), theme);
    m_app.settings().setValue(QStringLiteral("Appearance/size"), Style::sizeName(Style::instance()->sizeLevel()));

    Style::instance()->setLabels(labelRules());

    // Aliases: the table is the whole list of your own aliases.
    AliasManager &aliases = m_app.aliases();
    QStringList keep;
    for (int r = 0; r < m_aliases->rowCount(); ++r) {
        QString name = m_aliases->item(r, 0)->text().trimmed();
        if (name.startsWith(QLatin1Char('/')))
            name.remove(0, 1);
        const QString path = m_aliases->item(r, 1)->text().trimmed();
        if (name.isEmpty() && path.isEmpty())
            continue;
        QString why;
        if (!aliases.setUserAlias(name, path, &why)) {
            if (error)
                *error = QStringLiteral("Alias \"%1\": %2").arg(name, why);
            setPage(QStringLiteral("aliases"));
            return false;
        }
        keep << name.toLower();
    }
    for (const QString &name : aliases.names()) {
        if (aliases.isUserAlias(name) && !keep.contains(name))
            aliases.removeUserAlias(name);
    }
    aliases.save();

    FolderFormats::instance().setAutoSwitch(m_autoFormat->isChecked());
    m_originalTheme = Style::instance()->theme().name;
    m_originalThemeData = Style::instance()->theme();
    m_originalSize = Style::instance()->sizeLevel();
    return true;
}

void PreferencesDialog::accept()
{
    QString err;
    if (!apply(&err)) {
        QMessageBox::warning(this, QStringLiteral("Preferences"), err);
        return;
    }
    QDialog::accept();
}

void PreferencesDialog::reject()
{
    // Put back what was showing when the window opened (or at the last Apply).
    if (!Style::instance()->setTheme(m_originalTheme))
        Style::instance()->setTheme(m_originalThemeData);
    Style::instance()->setSizeLevel(m_originalSize);
    QDialog::reject();
}
