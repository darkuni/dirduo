#pragma once

#include <QWidget>

#include <functional>

class AliasManager;
class QCompleter;
class QMenu;
class QHBoxLayout;
class QLineEdit;
class QStackedLayout;
class QStringListModel;
class QToolButton;

// Address bar for one file display. Shows the path as clickable segments; the
// arrow after each segment drops down that folder's subfolders. Clicking empty
// space (or Ctrl+L) switches to a text field that completes paths and aliases.
class BreadcrumbBar : public QWidget
{
    Q_OBJECT
public:
    explicit BreadcrumbBar(AliasManager &aliases, QWidget *parent = nullptr);

    void setPath(const QString &path);
    QString path() const { return m_path; }
    void startEditing();
    bool isEditing() const;

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

Q_SIGNALS:
    // Raw text as typed or a full path from a segment; the lister resolves it.
    void navigateRequested(const QString &input);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Crumb
    {
        QString label;
        QString path;
        QToolButton *button = nullptr;
        QToolButton *arrow = nullptr;
        QString icon;
        bool canList = true; // sibling dropdown (local folders only)
    };

    void appendLocalCrumbs(const QString &path);

    void rebuild();
    void relayout();
    void stopEditing();
    void updateCompletions(const QString &text);
    void fillChildMenu(QToolButton *arrow, const QString &dir);
    void addChildActions(QMenu *menu, const QStringList &names, const std::function<QString(const QString &)> &pathOf);

    AliasManager &m_aliases;
    QString m_path;
    QList<Crumb> m_crumbs;

    QStackedLayout *m_stack;
    QWidget *m_crumbArea;
    QHBoxLayout *m_crumbLayout;
    QToolButton *m_overflow;
    QLineEdit *m_edit;
    QCompleter *m_completer;
    QStringListModel *m_completions;
};
