#include "ui/Style.h"

#include "core/Paths.h"

#include <QApplication>
#include <QFileInfo>
#include <QIcon>
#include <QDir>
#include <QSettings>
#include <QStyleHints>

namespace {

Style *g_style = nullptr;

void setTypes(Theme &t, const char *folder, const char *image, const char *audio, const char *video, const char *archive,
              const char *disk, const char *doc, const char *code, const char *prog)
{
    using C = FileKind::Category;
    t.types.insert(C::Folder, QColor(QLatin1String(folder)));
    t.types.insert(C::Image, QColor(QLatin1String(image)));
    t.types.insert(C::Audio, QColor(QLatin1String(audio)));
    t.types.insert(C::Video, QColor(QLatin1String(video)));
    t.types.insert(C::Archive, QColor(QLatin1String(archive)));
    t.types.insert(C::DiskImage, QColor(QLatin1String(disk)));
    t.types.insert(C::Document, QColor(QLatin1String(doc)));
    t.types.insert(C::Code, QColor(QLatin1String(code)));
    t.types.insert(C::Executable, QColor(QLatin1String(prog)));
}

Theme themeLight()
{
    Theme t;
    t.name = QStringLiteral("Linopus Light");
    t.window = QColor::fromRgb(0xeef1f5);
    t.base = QColor::fromRgb(0xffffff);
    t.alternate = QColor::fromRgb(0xf5f7fa);
    t.text = QColor::fromRgb(0x1f2933);
    t.dimText = QColor::fromRgb(0x8a94a3);
    t.highlight = QColor::fromRgb(0x2f6fdf);
    t.highlightText = QColor::fromRgb(0xffffff);
    t.button = QColor::fromRgb(0xe4e8ee);
    t.sourceAccent = QColor::fromRgb(0x2f6fdf);
    t.destAccent = QColor::fromRgb(0x1f9d6b);
    t.lockedAccent = QColor::fromRgb(0xd97706);
    t.groupHeader = QColor::fromRgb(0xe6ecf5);
    t.sizeBar = QColor::fromRgb(0x7aa7f0);
    setTypes(t, "#1d4ed8", "#a21caf", "#047857", "#b91c1c", "#b45309", "#c2410c", "#334155", "#0f766e", "#7c3aed");
    return t;
}

Theme themeDark()
{
    Theme t;
    t.name = QStringLiteral("Linopus Dark");
    t.dark = true;
    t.window = QColor::fromRgb(0x1b1f27);
    t.base = QColor::fromRgb(0x12151b);
    t.alternate = QColor::fromRgb(0x171b22);
    t.text = QColor::fromRgb(0xdce3ec);
    t.dimText = QColor::fromRgb(0x6b7686);
    t.highlight = QColor::fromRgb(0x3b6fd4);
    t.highlightText = QColor::fromRgb(0xffffff);
    t.button = QColor::fromRgb(0x262c36);
    t.sourceAccent = QColor::fromRgb(0x4c8dff);
    t.destAccent = QColor::fromRgb(0x34c38f);
    t.lockedAccent = QColor::fromRgb(0xf59e0b);
    t.groupHeader = QColor::fromRgb(0x232a35);
    t.sizeBar = QColor::fromRgb(0x3a5f9e);
    setTypes(t, "#7fb0ff", "#e879f9", "#4ade80", "#f87171", "#fbbf24", "#fb923c", "#cbd5e1", "#2dd4bf", "#a78bfa");
    return t;
}

Theme themeWorkbench()
{
    // Amiga Workbench 1.3: blue, white, black and orange.
    Theme t;
    t.name = QStringLiteral("Workbench");
    t.dark = true;
    t.window = QColor::fromRgb(0x0055aa);
    t.base = QColor::fromRgb(0x0055aa);
    t.alternate = QColor::fromRgb(0x004d9c);
    t.text = QColor::fromRgb(0xffffff);
    t.dimText = QColor::fromRgb(0x9cc0e8);
    t.highlight = QColor::fromRgb(0xff8800);
    t.highlightText = QColor::fromRgb(0x000000);
    t.button = QColor::fromRgb(0x0066cc);
    t.sourceAccent = QColor::fromRgb(0xff8800);
    t.destAccent = QColor::fromRgb(0xffffff);
    t.lockedAccent = QColor::fromRgb(0x000000);
    t.groupHeader = QColor::fromRgb(0x003f80);
    t.sizeBar = QColor::fromRgb(0xff8800);
    setTypes(t, "#ffffff", "#ffbb66", "#ffffff", "#ffffff", "#ffbb66", "#ff8800", "#ffffff", "#ffffff", "#ffbb66");
    return t;
}

Theme themeSystem()
{
    Theme t = themeLight();
    t.name = QStringLiteral("Follow Desktop");
    t.followSystem = true;
    return t;
}

QString colorName(const QColor &c)
{
    return c.isValid() ? c.name() : QString();
}

} // namespace

// ---------------------------------------------------------------- Theme

QStringList Theme::builtinNames()
{
    return {QStringLiteral("Follow Desktop"), QStringLiteral("Linopus Light"), QStringLiteral("Linopus Dark"),
            QStringLiteral("Workbench")};
}

Theme Theme::builtin(const QString &name)
{
    if (name == QLatin1String("Linopus Dark"))
        return themeDark();
    if (name == QLatin1String("Workbench"))
        return themeWorkbench();
    if (name == QLatin1String("Follow Desktop"))
        return themeSystem();
    return themeLight();
}

QColor Theme::typeColor(FileKind::Category c) const
{
    if (!colorFileTypes)
        return {};
    return types.value(c);
}

QPalette Theme::palette(const QPalette &systemPalette) const
{
    if (followSystem)
        return systemPalette;
    QPalette p = systemPalette;
    for (QPalette::ColorGroup g : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        p.setColor(g, QPalette::Window, window);
        p.setColor(g, QPalette::Base, base);
        p.setColor(g, QPalette::AlternateBase, alternate);
        p.setColor(g, QPalette::Button, button);
        p.setColor(g, QPalette::ToolTipBase, base);
        p.setColor(g, QPalette::Highlight, highlight);
        p.setColor(g, QPalette::HighlightedText, highlightText);
        p.setColor(g, QPalette::Link, sourceAccent);
        p.setColor(g, QPalette::PlaceholderText, dimText);
        const QColor fg = g == QPalette::Disabled ? dimText : text;
        p.setColor(g, QPalette::WindowText, fg);
        p.setColor(g, QPalette::Text, fg);
        p.setColor(g, QPalette::ButtonText, fg);
        p.setColor(g, QPalette::ToolTipText, text);
        p.setColor(g, QPalette::BrightText, highlightText);
        // Bevels for styles that draw them (Fusion).
        p.setColor(g, QPalette::Light, button.lighter(dark ? 130 : 112));
        p.setColor(g, QPalette::Midlight, button.lighter(dark ? 115 : 106));
        p.setColor(g, QPalette::Mid, button.darker(dark ? 120 : 112));
        p.setColor(g, QPalette::Dark, button.darker(dark ? 150 : 130));
        p.setColor(g, QPalette::Shadow, QColor(0, 0, 0, dark ? 160 : 90));
    }
    p.setColor(QPalette::Inactive, QPalette::Highlight, highlight.lighter(dark ? 80 : 125));
    return p;
}

bool Theme::load(const QString &file, Theme &out)
{
    if (!QFileInfo::exists(file))
        return false;
    QSettings s(file, QSettings::IniFormat);
    Theme t = themeLight();
    s.beginGroup(QStringLiteral("Theme"));
    t.name = s.value(QStringLiteral("name"), QFileInfo(file).completeBaseName()).toString();
    t.dark = s.value(QStringLiteral("dark"), false).toBool();
    if (t.dark)
        t = themeDark(), t.name = s.value(QStringLiteral("name"), QFileInfo(file).completeBaseName()).toString();
    auto color = [&s](const char *key, QColor &into) {
        const QColor c(s.value(QLatin1String(key)).toString());
        if (c.isValid())
            into = c;
    };
    color("window", t.window);
    color("base", t.base);
    color("alternate", t.alternate);
    color("text", t.text);
    color("dimText", t.dimText);
    color("highlight", t.highlight);
    color("highlightText", t.highlightText);
    color("button", t.button);
    color("sourceAccent", t.sourceAccent);
    color("destAccent", t.destAccent);
    color("lockedAccent", t.lockedAccent);
    color("groupHeader", t.groupHeader);
    color("sizeBar", t.sizeBar);
    t.colorFileTypes = s.value(QStringLiteral("colorFileTypes"), true).toBool();
    s.endGroup();
    s.beginGroup(QStringLiteral("FileTypes"));
    const QStringList keys = s.childKeys();
    for (const QString &k : keys) {
        FileKind::Category c;
        const QColor col(s.value(k).toString());
        if (FileKind::categoryFromKey(k, c) && col.isValid())
            t.types.insert(c, col);
    }
    s.endGroup();
    out = t;
    return true;
}

bool Theme::save(const QString &file) const
{
    QSettings s(file, QSettings::IniFormat);
    s.clear();
    s.beginGroup(QStringLiteral("Theme"));
    s.setValue(QStringLiteral("name"), name);
    s.setValue(QStringLiteral("dark"), dark);
    s.setValue(QStringLiteral("window"), colorName(window));
    s.setValue(QStringLiteral("base"), colorName(base));
    s.setValue(QStringLiteral("alternate"), colorName(alternate));
    s.setValue(QStringLiteral("text"), colorName(text));
    s.setValue(QStringLiteral("dimText"), colorName(dimText));
    s.setValue(QStringLiteral("highlight"), colorName(highlight));
    s.setValue(QStringLiteral("highlightText"), colorName(highlightText));
    s.setValue(QStringLiteral("button"), colorName(button));
    s.setValue(QStringLiteral("sourceAccent"), colorName(sourceAccent));
    s.setValue(QStringLiteral("destAccent"), colorName(destAccent));
    s.setValue(QStringLiteral("lockedAccent"), colorName(lockedAccent));
    s.setValue(QStringLiteral("groupHeader"), colorName(groupHeader));
    s.setValue(QStringLiteral("sizeBar"), colorName(sizeBar));
    s.setValue(QStringLiteral("colorFileTypes"), colorFileTypes);
    s.endGroup();
    s.beginGroup(QStringLiteral("FileTypes"));
    for (auto it = types.cbegin(); it != types.cend(); ++it)
        s.setValue(FileKind::categoryKey(it.key()), colorName(it.value()));
    s.endGroup();
    s.sync();
    return s.status() == QSettings::NoError;
}

// ---------------------------------------------------------------- Style

Style::Style()
    : m_theme(themeLight())
{
}

Style *Style::instance()
{
    if (!g_style)
        g_style = new Style;
    return g_style;
}

QString Style::themesDir() const
{
    const QString dir = Paths::configDir() + QStringLiteral("/themes");
    QDir().mkpath(dir);
    return dir;
}

QStringList Style::themeNames() const
{
    QStringList names = Theme::builtinNames();
    const QStringList files = QDir(themesDir()).entryList({QStringLiteral("*.ini")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        Theme t;
        if (Theme::load(QDir(themesDir()).filePath(f), t) && !names.contains(t.name))
            names << t.name;
    }
    return names;
}

bool Style::setTheme(const QString &name)
{
    if (Theme::builtinNames().contains(name)) {
        setTheme(Theme::builtin(name));
        return true;
    }
    const QStringList files = QDir(themesDir()).entryList({QStringLiteral("*.ini")}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        Theme t;
        if (Theme::load(QDir(themesDir()).filePath(f), t) && (t.name == name || QFileInfo(f).completeBaseName() == name)) {
            setTheme(t);
            return true;
        }
    }
    return false;
}

void Style::setTheme(const Theme &theme)
{
    m_theme = theme;
    apply();
    Q_EMIT changed();
}

void Style::reloadLabels()
{
    m_labels.load();
    Q_EMIT changed();
}

void Style::setLabels(const QList<LabelRule> &rules)
{
    m_labels.setRules(rules);
    m_labels.save();
    Q_EMIT changed();
}

void Style::apply()
{
    if (!qApp)
        return;
    if (!m_haveSystemPalette) {
        m_systemPalette = QApplication::palette();
        m_haveSystemPalette = true;
    }
    if (m_theme.followSystem) {
        // Follow the desktop, including its own light or dark choice.
        m_theme.dark = m_systemPalette.color(QPalette::Window).lightness() < 128;
        if (m_theme.dark) {
            const Theme d = Theme::builtin(QStringLiteral("Linopus Dark"));
            m_theme.types = d.types;
            m_theme.dimText = d.dimText;
            m_theme.groupHeader = m_systemPalette.color(QPalette::AlternateBase);
        } else {
            m_theme.groupHeader = m_systemPalette.color(QPalette::AlternateBase).darker(104);
        }
        m_theme.sourceAccent = m_systemPalette.color(QPalette::Highlight);
    }
    QApplication::setPalette(m_theme.palette(m_systemPalette));

    // Icons drawn for a light background vanish on a dark one, and the other
    // way round: pick the matching variant of the icon theme when it exists.
    static QString desktopIcons;
    if (desktopIcons.isEmpty())
        desktopIcons = QIcon::themeName();
    auto exists = [](const QString &name) {
        for (const QString &dir : QIcon::themeSearchPaths()) {
            if (QFileInfo::exists(dir + QLatin1Char('/') + name + QStringLiteral("/index.theme")))
                return true;
        }
        return false;
    };
    QString want = desktopIcons;
    if (!m_theme.followSystem) {
        QString base = desktopIcons;
        if (base.endsWith(QLatin1String("-dark")))
            base.chop(5);
        const QString dark = base + QStringLiteral("-dark");
        want = m_theme.dark && exists(dark) ? dark : base;
        if (!exists(want))
            want = desktopIcons;
    }
    if (!want.isEmpty() && want != QIcon::themeName())
        QIcon::setThemeName(want);
}

double Style::scale() const
{
    static const double factors[] = {1.0, 1.15, 1.3};
    return factors[qBound(0, m_sizeLevel, 2)];
}

QString Style::sizeName(int level)
{
    switch (level) {
    case 1:
        return QStringLiteral("large");
    case 2:
        return QStringLiteral("larger");
    default:
        return QStringLiteral("normal");
    }
}

int Style::sizeFromName(const QString &name)
{
    const QString n = name.toLower();
    if (n == QLatin1String("normal") || n == QLatin1String("default"))
        return 0;
    if (n == QLatin1String("large"))
        return 1;
    if (n == QLatin1String("larger") || n == QLatin1String("largest"))
        return 2;
    return -1;
}

void Style::setSizeLevel(int level)
{
    level = qBound(0, level, 2);
    if (!qApp)
        return;
    if (!m_haveBaseFont) {
        m_baseFont = QApplication::font();
        m_haveBaseFont = true;
    }
    m_sizeLevel = level;
    QFont f = m_baseFont;
    if (f.pointSizeF() > 0)
        f.setPointSizeF(m_baseFont.pointSizeF() * scale());
    else
        f.setPixelSize(int(m_baseFont.pixelSize() * scale() + 0.5));
    QApplication::setFont(f);
    Q_EMIT changed();
}
