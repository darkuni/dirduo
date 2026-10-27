#pragma once

#include <QObject>
#include <QString>

// General settings read often (the status line, copies, the terminal):
// kept in memory, saved to dirduo.ini, changed() when they change.
class Prefs : public QObject
{
    Q_OBJECT
public:
    static Prefs *instance();

    struct Values
    {
        bool statusCounts = true;    // "3 folders, 12 files"
        bool statusSelection = true; // "- 2 selected (4 MB)"
        bool statusFree = true;      // "12 GB free of 64 GB"
        bool askOnError = true;      // Retry / Skip / Abort when a copy fails
        bool junkHidden = true;      // .DS_Store, Thumbs.db... count as hidden files
        bool treeLines = true;       // folder trees: dotted branch lines and + / - boxes
        bool treePath = true;        // folder trees: the way to the current folder in the display's color
        // The status bar's definition (StatusFormat); empty: made from the
        // three switches above.
        QString statusDefinition;
        QString effectiveStatusDefinition() const;
        QString terminal;            // empty: Konsole, else the first one installed
        bool notifyJobs = true;      // a desktop notice when a job finishes while DirDuo is in the background
        int notifyAfter = 10;        // ...and it took at least this many seconds

        // The standalone viewer (Show), as Opus's Viewer pages.
        struct Viewer
        {
            bool openPictures = false;   // double-clicking a picture opens it in the viewer
            bool openMedia = false;      // ...music and video too
            bool siblings = true;        // Next/Previous go through the folder (Opus "Generate Next/Previous list")
            bool reuse = true;           // open in the viewer already open (Opus "Recycle viewer window")
            bool wrap = false;           // after the last one, back to the first
            bool wheelSteps = true;      // the wheel goes to the next file; false: it zooms
            int resetZoom = 0;           // each new picture: 0 fit, 1 grow, 2 original size
            int background = 0;          // 0 dark, 1 black, 2 white, 3 the theme's, 4 checkerboard
            bool fullPathTitle = false;
            bool playNext = true;        // music/video: go on to the next file when one ends
            bool autoPlay = true;        // music/video starts playing when shown
            double slideSeconds = 4.0;
            bool slideRandom = false;
            bool slideAuto = false;      // start a slideshow whenever it opens with several pictures
            QString markCollection = QStringLiteral("Marked Pictures - %F"); // %F folder, %D date
            bool openMarkedOnClose = true;
        } viewer;
    };

    const Values &values() const { return m_values; }
    void setValues(const Values &v); // saves and tells everyone
    void reload();

Q_SIGNALS:
    void changed();

private:
    Prefs();
    Values m_values;
};
