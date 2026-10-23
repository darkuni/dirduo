#pragma once

#include "core/LabelRules.h"
#include "ui/Style.h"

#include <QDialog>
#include <QToolButton>

class AppContext;
class ListerWindow;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;

// A button showing a color; click to pick another.
class ColorButton : public QToolButton
{
    Q_OBJECT
public:
    explicit ColorButton(QWidget *parent = nullptr);
    void setColor(const QColor &c);
    QColor color() const { return m_color; }
    void setAllowNone(bool on) { m_allowNone = on; } // right-click clears

Q_SIGNALS:
    void colorChanged(const QColor &c);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QColor m_color;
    bool m_allowNone = false;
};

// Preferences: every setting in one window, with a search box over all of
// them. Theme changes show at once and are undone by Cancel.
class PreferencesDialog : public QDialog
{
    Q_OBJECT
public:
    PreferencesDialog(AppContext &app, ListerWindow *lister, const QString &page = {});
    ~PreferencesDialog() override;

    static QStringList pageNames(); // appearance, theme, labels, aliases, formats, toolbars
    bool setPage(const QString &name);
    QString currentPage() const;
    // Pages matching the search text (all when empty); the list shows only these.
    QStringList search(const QString &text);

    // Theme editor, for tests.
    Theme editedTheme() const { return m_theme; }
    void setThemeColor(const QString &key, const QColor &c);
    void setThemeName(const QString &name);
    bool saveTheme(QString *error = nullptr);

    // Labels, for tests.
    QList<LabelRule> labelRules() const;
    void addLabelRule(const LabelRule &rule);

    // Aliases, for tests.
    void setAliasRow(int row, const QString &name, const QString &path);
    int addAliasRow();

    bool apply(QString *error = nullptr);
    void accept() override;
    void reject() override;

private:
    QWidget *buildAppearance();
    QWidget *buildTheme();
    QWidget *buildLabels();
    QWidget *buildAliases();
    QWidget *buildFormats();
    QWidget *buildToolbars();
    void addPage(const QString &key, const QString &title, const QString &icon, QWidget *page, const QStringList &keywords);
    void loadThemeIntoEditor(const Theme &t);
    void previewTheme();
    void refreshThemeLists(const QString &select = {});
    void fillLabelRow(int row, const LabelRule &r);
    void refreshFormats();

    AppContext &m_app;
    ListerWindow *m_lister;
    QListWidget *m_nav;
    QStackedWidget *m_stack;
    QLineEdit *m_search;
    QStringList m_keys;
    QList<QStringList> m_keywords;

    // Undo for Cancel.
    QString m_originalTheme;
    Theme m_originalThemeData;
    int m_originalSize = 0;

    QComboBox *m_themeBox = nullptr;
    QComboBox *m_sizeBox = nullptr;
    QComboBox *m_startFrom = nullptr;
    QLineEdit *m_themeName = nullptr;
    QCheckBox *m_themeDark = nullptr;
    QCheckBox *m_colorTypes = nullptr;
    QLabel *m_themeStatus = nullptr;
    QHash<QString, ColorButton *> m_colorButtons;
    Theme m_theme;
    bool m_themeDirty = false;
    bool m_loading = false;

    QTableWidget *m_labels = nullptr;
    QTableWidget *m_aliases = nullptr;
    QTreeWidget *m_formats = nullptr;
    QCheckBox *m_autoFormat = nullptr;
    QLabel *m_defaultFormat = nullptr;
};
