#include "ui/DrivesBar.h"
#include <QToolButton>
#include <QMenu>

#include "core/FileOps.h"
#include "ui/ButtonBar.h"
#include "ui/Style.h"

#include <QTimer>

namespace {

QString kindWord(DriveEntry::Kind k)
{
    switch (k) {
    case DriveEntry::Kind::Root:
        return QStringLiteral("System root");
    case DriveEntry::Kind::Home:
        return QStringLiteral("Home folder");
    case DriveEntry::Kind::SdCard:
        return QStringLiteral("SD card");
    case DriveEntry::Kind::Usb:
        return QStringLiteral("Removable drive");
    case DriveEntry::Kind::Disk:
        return QStringLiteral("Disk");
    }
    return {};
}

} // namespace

DrivesBar::DrivesBar(QWidget *parent)
    : QToolBar(QStringLiteral("Drives"), parent)
{
    setObjectName(QStringLiteral("toolbar-drives"));
    setIconSize(QSize(Style::instance()->scaled(18), Style::instance()->scaled(18)));
    connect(Style::instance(), &Style::changed, this, [this]() {
        setIconSize(QSize(Style::instance()->scaled(18), Style::instance()->scaled(18)));
    });
    m_poll.setInterval(2000);
    connect(&m_poll, &QTimer::timeout, this, &DrivesBar::rescan);
    rescan();
    m_poll.start();
}

QString DrivesBar::quoted(const QString &path)
{
    return QLatin1Char('"') + path + QLatin1Char('"');
}

void DrivesBar::rescan()
{
    const QList<DriveEntry> fresh = DriveList::scan();
    if (fresh == m_drives) {
        // Same drives; free space may have moved.
        m_drives = fresh;
        updateTips();
        return;
    }
    m_drives = fresh;
    rebuild();
}

void DrivesBar::setDrivesForTesting(const QList<DriveEntry> &drives)
{
    m_poll.stop();
    m_drives = drives;
    rebuild();
}

void DrivesBar::rebuild()
{
    clear(); // deletes the old buttons' actions and widgets
    m_buttons.clear();
    for (const DriveEntry &d : std::as_const(m_drives)) {
        ButtonDef def;
        def.label = d.name;
        def.icon = DriveList::iconName(d.kind);
        def.left = QStringLiteral("Go ") + quoted(d.path);
        def.middle = QStringLiteral("Go ") + quoted(d.path) + QStringLiteral(" NEWTAB");
        def.right = QStringLiteral("Go ") + quoted(d.path) + QStringLiteral(" OPENINDEST");
        auto *b = new ThreeButton(def, this);
        if (b->icon().isNull() && d.kind == DriveEntry::Kind::Root)
            b->setIcon(QIcon::fromTheme(QStringLiteral("drive-harddisk")));
        if (b->icon().isNull() && d.kind == DriveEntry::Kind::SdCard)
            b->setIcon(QIcon::fromTheme(QStringLiteral("media-flash")));
        if (!b->icon().isNull())
            b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCheckable(true);
        connect(b, &ThreeButton::commandTriggered, this, [this](const QString &cmd) {
            Q_EMIT commandTriggered(cmd);
            // A click toggles the button; put the marker back where it belongs.
            QTimer::singleShot(0, this, [this]() { setActivePath(m_activePath); });
        });
        addWidget(b);
        m_buttons << b;
    }
    // Network: a click browses it; the arrow lists Windows shares and your servers.
    m_network = new QToolButton(this);
    m_network->setText(QStringLiteral("Network"));
    m_network->setIcon(QIcon::fromTheme(QStringLiteral("network-workgroup")));
    m_network->setToolButtonStyle(m_network->icon().isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
    m_network->setAutoRaise(true);
    m_network->setFocusPolicy(Qt::NoFocus);
    m_network->setToolTip(QStringLiteral("<b>Network</b><br>Windows and Samba machines and their shares. The arrow: your saved servers."));
    m_network->setPopupMode(QToolButton::MenuButtonPopup);
    auto *menu = new QMenu(m_network);
    connect(menu, &QMenu::aboutToShow, this, [this, menu]() {
        menu->clear();
        if (m_networkMenu)
            m_networkMenu(menu);
    });
    m_network->setMenu(menu);
    connect(m_network, &QToolButton::clicked, this, [this]() { Q_EMIT commandTriggered(QStringLiteral("Go smb:/")); });
    addWidget(m_network);
    updateTips();
    setActivePath(m_activePath);
}

void DrivesBar::updateTips()
{
    for (int i = 0; i < m_buttons.size() && i < m_drives.size(); ++i) {
        const DriveEntry &d = m_drives.at(i);
        QStringList lines;
        lines << QStringLiteral("<b>%1</b> - %2").arg(d.name.toHtmlEscaped(), kindWord(d.kind));
        lines << d.path.toHtmlEscaped();
        if (d.bytesTotal > 0)
            lines << QStringLiteral("%1 free of %2").arg(FileOps::formatSize(d.bytesFree), FileOps::formatSize(d.bytesTotal));
        lines << QStringLiteral("<i>Click: open here. Middle-click: new tab. Right-click: open in the destination.</i>");
        m_buttons.at(i)->setToolTip(lines.join(QStringLiteral("<br>")));
    }
}

void DrivesBar::setActivePath(const QString &path)
{
    m_activePath = path;
    const int active = path.isEmpty() ? -1 : DriveList::indexContaining(m_drives, path);
    for (int i = 0; i < m_buttons.size(); ++i)
        m_buttons.at(i)->setChecked(i == active);
}
