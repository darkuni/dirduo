#pragma once

#include <QList>
#include <QString>
#include <QToolBar>
#include <QToolButton>

// A toolbar button that runs a different command for each mouse button,
// like Opus three-button buttons.
struct ButtonDef
{
    QString label;
    QString icon;
    QString tip;
    QString left;
    QString right;
    QString middle;
    bool separator = false;
};

class ThreeButton : public QToolButton
{
    Q_OBJECT
public:
    ThreeButton(const ButtonDef &def, QWidget *parent = nullptr);
    const ButtonDef &definition() const { return m_def; }

Q_SIGNALS:
    void commandTriggered(const QString &command);

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    ButtonDef m_def;
    Qt::MouseButton m_pressed = Qt::NoButton;
};

// Phase 1 reads ~/.config/linopus/toolbars/<name>.json when present and falls
// back to the built-in set. The editor arrives in phase 3.
class ButtonBar : public QToolBar
{
    Q_OBJECT
public:
    ButtonBar(const QString &name, const QList<ButtonDef> &buttons, QWidget *parent = nullptr);

    static QList<ButtonDef> defaultButtons();
    static QList<ButtonDef> load(const QString &file, bool *ok);
    static bool save(const QString &file, const QList<ButtonDef> &buttons);

Q_SIGNALS:
    void commandTriggered(const QString &command);
};
