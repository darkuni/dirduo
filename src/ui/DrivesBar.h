#pragma once

#include "core/DriveList.h"

#include <QTimer>
#include <QToolBar>

class ThreeButton;

// Opus-style drives toolbar: Root, Home, then every SD card, USB stick and
// extra disk that is mounted right now. It updates itself as cards and sticks
// come and go, and marks the drive the source folder is on.
//
//   left-click    open in the source display
//   middle-click  open in a new tab
//   right-click   open in the destination display
class DrivesBar : public QToolBar
{
    Q_OBJECT
public:
    explicit DrivesBar(QWidget *parent = nullptr);

    void setActivePath(const QString &path);
    const QList<DriveEntry> &drives() const { return m_drives; }

    // Re-reads the mounts now instead of waiting for the next poll.
    void rescan();

    // For tests: show this list and stop polling.
    void setDrivesForTesting(const QList<DriveEntry> &drives);

Q_SIGNALS:
    void commandTriggered(const QString &command);

private:
    void rebuild();
    void updateTips();
    static QString quoted(const QString &path);

    QList<DriveEntry> m_drives;
    QList<ThreeButton *> m_buttons;
    QString m_activePath;
    QTimer m_poll;
};
