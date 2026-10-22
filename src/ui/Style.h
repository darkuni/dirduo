#pragma once

#include "core/FileKind.h"
#include "core/LabelRules.h"

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPalette>
#include <QString>
#include <QStringList>

// A look for the whole application: window colors, the SOURCE and DEST
// accents, and a color per file type. Built-in themes plus any .ini file in
// ~/.config/linopus/themes/ (same keys as save() writes, so a theme can be
// copied, edited and shared).
struct Theme
{
    QString name;
    bool followSystem = false; // keep the desktop's palette, add only accents
    bool dark = false;

    QColor window;
    QColor base;       // file list background
    QColor alternate;  // every other row
    QColor text;
    QColor dimText;    // hidden files, secondary details
    QColor highlight;  // selection
    QColor highlightText;
    QColor button;
    QColor sourceAccent;
    QColor destAccent;
    QColor lockedAccent;
    QColor groupHeader; // grouping bands
    QColor sizeBar;     // folder size bars
    bool colorFileTypes = true;
    QHash<FileKind::Category, QColor> types;

    QPalette palette(const QPalette &systemPalette) const;
    QColor typeColor(FileKind::Category c) const;

    static QStringList builtinNames();
    static Theme builtin(const QString &name);
    static bool load(const QString &file, Theme &out);
    bool save(const QString &file) const;
};

// The current theme and label rules, shared by every lister. Views repaint
// when either changes.
class Style : public QObject
{
    Q_OBJECT
public:
    static Style *instance();

    const Theme &theme() const { return m_theme; }
    const LabelRules &labels() const { return m_labels; }

    // Built-in names, then themes found in ~/.config/linopus/themes.
    QStringList themeNames() const;
    bool setTheme(const QString &name);
    void setTheme(const Theme &theme);
    void reloadLabels();
    void setLabels(const QList<LabelRule> &rules);

    // Applies the palette to the application. Called once at start and on
    // every theme change.
    void apply();

    QString themesDir() const;

Q_SIGNALS:
    void changed();

private:
    Style();
    Theme m_theme;
    LabelRules m_labels;
    QPalette m_systemPalette;
    bool m_haveSystemPalette = false;
};
