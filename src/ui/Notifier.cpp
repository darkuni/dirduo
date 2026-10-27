#include "ui/Notifier.h"

#ifdef DIRDUO_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif

#include <QGuiApplication>

namespace {
const char *kService = "org.freedesktop.Notifications";
const char *kPath = "/org/freedesktop/Notifications";
} // namespace

Notifier *Notifier::instance()
{
    static Notifier *n = new Notifier;
    return n;
}

Notifier::Notifier() = default;

bool Notifier::show(const Notice &n)
{
    if (m_sink) {
        ++m_lastId;
        m_actions.insert(m_lastId, n.onClick);
        m_sink(n);
        return true;
    }
#ifdef DIRDUO_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return false;
    if (!m_connected) {
        bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath), QString::fromLatin1(kService),
                    QStringLiteral("ActionInvoked"), this, SLOT(onActionInvoked(uint, QString)));
        bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath), QString::fromLatin1(kService),
                    QStringLiteral("NotificationClosed"), this, SLOT(onClosed(uint, uint)));
        m_connected = true;
    }
    QDBusInterface iface(QString::fromLatin1(kService), QString::fromLatin1(kPath), QString::fromLatin1(kService), bus);
    QStringList actions;
    if (n.onClick)
        actions << QStringLiteral("default") << QStringLiteral("Open Folder");
    QVariantMap hints;
    hints.insert(QStringLiteral("desktop-entry"), QStringLiteral(DIRDUO_APP_ID));
    // Notify(app_name, replaces_id, app_icon, summary, body, actions, hints, expire_timeout)
    const QDBusReply<uint> reply = iface.call(QStringLiteral("Notify"), QStringLiteral("Directory Duo"), uint(0),
                                              n.icon.isEmpty() ? QStringLiteral(DIRDUO_APP_ID) : n.icon, n.title, n.body, actions,
                                              hints, int(-1));
    if (!reply.isValid())
        return false;
    m_lastId = reply.value();
    if (n.onClick)
        m_actions.insert(m_lastId, n.onClick);
    return true;
#else
    Q_UNUSED(n)
    return false;
#endif
}

void Notifier::click(uint id)
{
    onActionInvoked(id, QStringLiteral("default"));
}

void Notifier::onActionInvoked(uint id, const QString &action)
{
    Q_UNUSED(action)
    const auto fn = m_actions.take(id);
    if (fn)
        fn();
}

void Notifier::onClosed(uint id, uint reason)
{
    Q_UNUSED(reason)
    m_actions.remove(id);
}
