#include "ui/ButtonBar.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QSaveFile>

namespace {

QString describe(const ButtonDef &d)
{
    QStringList lines;
    if (!d.tip.isEmpty())
        lines << d.tip;
    auto add = [&](const char *which, const QString &cmd) {
        if (!cmd.isEmpty())
            lines << QStringLiteral("%1: %2").arg(QLatin1String(which), cmd.split(QLatin1Char('\n')).join(QStringLiteral("; ")));
    };
    add("Left-click", d.left);
    add("Right-click", d.right);
    add("Middle-click", d.middle);
    return lines.join(QLatin1Char('\n'));
}

} // namespace

ThreeButton::ThreeButton(const ButtonDef &def, QWidget *parent)
    : QToolButton(parent)
    , m_def(def)
{
    setText(def.label);
    const QIcon icon = QIcon::fromTheme(def.icon);
    if (!icon.isNull()) {
        setIcon(icon);
        setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    } else {
        setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    setToolTip(describe(def));
    setAutoRaise(true);
    setFocusPolicy(Qt::NoFocus);
    // Right-click is a function here, not the toolbar's context menu.
    setContextMenuPolicy(Qt::PreventContextMenu);
}

void ThreeButton::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton || event->button() == Qt::MiddleButton) {
        m_pressed = event->button();
        setDown(true);
        event->accept();
        return;
    }
    m_pressed = Qt::LeftButton;
    QToolButton::mousePressEvent(event);
}

void ThreeButton::mouseReleaseEvent(QMouseEvent *event)
{
    const bool inside = rect().contains(event->position().toPoint());
    const Qt::MouseButton button = event->button();
    if (button == Qt::RightButton || button == Qt::MiddleButton) {
        setDown(false);
        event->accept();
        if (inside && button == m_pressed) {
            const QString cmd = button == Qt::RightButton ? m_def.right : m_def.middle;
            Q_EMIT commandTriggered(cmd.isEmpty() ? m_def.left : cmd);
        }
        m_pressed = Qt::NoButton;
        return;
    }
    QToolButton::mouseReleaseEvent(event);
    if (inside && button == Qt::LeftButton && m_pressed == Qt::LeftButton && !m_def.left.isEmpty())
        Q_EMIT commandTriggered(m_def.left);
    m_pressed = Qt::NoButton;
}

ButtonBar::ButtonBar(const QString &name, const QList<ButtonDef> &buttons, QWidget *parent)
    : QToolBar(name, parent)
{
    setObjectName(QStringLiteral("toolbar-") + name.toLower());
    setMovable(true);
    setIconSize(QSize(18, 18));
    for (const ButtonDef &def : buttons) {
        if (def.separator) {
            addSeparator();
            continue;
        }
        auto *b = new ThreeButton(def, this);
        connect(b, &ThreeButton::commandTriggered, this, &ButtonBar::commandTriggered);
        addWidget(b);
    }
}

QList<ButtonDef> ButtonBar::defaultButtons()
{
    auto b = [](const char *label, const char *icon, const char *tip, const char *left, const char *right = "",
                const char *middle = "") {
        ButtonDef d;
        d.label = QString::fromUtf8(label);
        d.icon = QString::fromUtf8(icon);
        d.tip = QString::fromUtf8(tip);
        d.left = QString::fromUtf8(left);
        d.right = QString::fromUtf8(right);
        d.middle = QString::fromUtf8(middle);
        return d;
    };
    ButtonDef sep;
    sep.separator = true;
    return {
        b("Copy", "edit-copy", "Copy the selection to the destination", "Copy", "Copy AS"),
        b("Move", "transform-move", "Move the selection to the destination", "Copy MOVE", "Copy MOVE AS"),
        b("Delete", "edit-delete", "Delete the selection", "Delete", "Delete NORECYCLE"),
        b("Rename", "edit-rename", "Rename in place; arrow keys move to the next item", "Rename"),
        b("New Folder", "folder-new", "Create folders (separate several with commas)", "CreateFolder"),
        sep,
        b("Swap", "view-split-left-right", "Swap the folders in the two displays", "Go SWAP"),
        b("Same", "go-jump", "Open the source folder in the destination", "Go {sourcepath} OPENINDEST"),
        b("Vertical", "view-split-left-right", "Dual vertical: displays side by side (right-click: single display)",
          "Set DUAL=vertical", "Set DUAL=off"),
        b("Horizontal", "view-split-top-bottom", "Dual horizontal: displays top and bottom (right-click: single display)",
          "Set DUAL=horizontal", "Set DUAL=off"),
        b("Lock Dest", "object-locked", "Keep the destination where it is", "Set DESTLOCK=toggle"),
        b("Hidden", "view-hidden", "Show or hide hidden files", "Set HIDDEN=toggle"),
    };
}

QList<ButtonDef> ButtonBar::load(const QString &file, bool *ok)
{
    if (ok)
        *ok = false;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    QList<ButtonDef> out;
    const QJsonArray arr = doc.object().value(QStringLiteral("buttons")).toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        ButtonDef d;
        d.separator = o.value(QStringLiteral("separator")).toBool();
        d.label = o.value(QStringLiteral("label")).toString();
        d.icon = o.value(QStringLiteral("icon")).toString();
        d.tip = o.value(QStringLiteral("tip")).toString();
        d.left = o.value(QStringLiteral("left")).toString();
        d.right = o.value(QStringLiteral("right")).toString();
        d.middle = o.value(QStringLiteral("middle")).toString();
        if (d.separator || !d.label.isEmpty())
            out << d;
    }
    if (ok)
        *ok = true;
    return out;
}

bool ButtonBar::save(const QString &file, const QList<ButtonDef> &buttons)
{
    QJsonArray arr;
    for (const ButtonDef &d : buttons) {
        QJsonObject o;
        if (d.separator) {
            o.insert(QStringLiteral("separator"), true);
        } else {
            o.insert(QStringLiteral("label"), d.label);
            if (!d.icon.isEmpty())
                o.insert(QStringLiteral("icon"), d.icon);
            if (!d.tip.isEmpty())
                o.insert(QStringLiteral("tip"), d.tip);
            o.insert(QStringLiteral("left"), d.left);
            if (!d.right.isEmpty())
                o.insert(QStringLiteral("right"), d.right);
            if (!d.middle.isEmpty())
                o.insert(QStringLiteral("middle"), d.middle);
        }
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("buttons"), arr);
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}
