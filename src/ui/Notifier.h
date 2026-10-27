#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

// Desktop notifications (the freedesktop notification service, which KDE,
// GNOME and the Flatpak sandbox all have): a notice when a long file job
// finishes while DirDuo is in the background. Clicking it runs the action
// given with it (open the folder).
class Notifier : public QObject
{
    Q_OBJECT
public:
    static Notifier *instance();
    struct Notice
    {
        QString title;
        QString body;
        QString icon;
        std::function<void()> onClick;
    };
    // Shows it; false when there is no notification service.
    bool show(const Notice &n);
    // Tests: notices go here instead of to the desktop.
    void setSink(std::function<void(const Notice &)> sink) { m_sink = std::move(sink); }
    // Tests: as if the notice with this number was clicked.
    void click(uint id);
    uint lastId() const { return m_lastId; }

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &action);
    void onClosed(uint id, uint reason);

private:
    Notifier();
    QHash<uint, std::function<void()>> m_actions;
    std::function<void(const Notice &)> m_sink;
    uint m_lastId = 0;
    bool m_connected = false;
};
