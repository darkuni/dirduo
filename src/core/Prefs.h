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
