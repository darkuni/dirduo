#include "core/LabelRules.h"

#include "core/Paths.h"

#include <QSettings>

void LabelRule::compile()
{
    m_compiled.clear();
    const QStringList parts = pattern.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &p : parts) {
        m_compiled << QRegularExpression(QRegularExpression::wildcardToRegularExpression(p.trimmed()),
                                         QRegularExpression::CaseInsensitiveOption);
    }
}

bool LabelRule::matches(const QString &name, bool isDir) const
{
    if (applies == Applies::Files && isDir)
        return false;
    if (applies == Applies::Folders && !isDir)
        return false;
    for (const QRegularExpression &re : m_compiled) {
        if (re.match(name).hasMatch())
            return true;
    }
    return false;
}

LabelRules::LabelRules(const QString &file)
    : m_file(file)
{
    load();
}

void LabelRules::load()
{
    m_rules.clear();
    QSettings s(m_file.isEmpty() ? Paths::configFile(QStringLiteral("labels.ini")) : m_file, QSettings::IniFormat);
    QStringList groups = s.childGroups();
    std::sort(groups.begin(), groups.end(), [](const QString &a, const QString &b) { return a.toInt() < b.toInt(); });
    for (const QString &g : groups) {
        s.beginGroup(g);
        LabelRule r;
        r.pattern = s.value(QStringLiteral("pattern")).toString();
        r.color = QColor(s.value(QStringLiteral("color")).toString());
        r.background = QColor(s.value(QStringLiteral("background")).toString());
        r.bold = s.value(QStringLiteral("bold"), false).toBool();
        r.italic = s.value(QStringLiteral("italic"), false).toBool();
        const QString applies = s.value(QStringLiteral("applies"), QStringLiteral("both")).toString().toLower();
        r.applies = applies == QLatin1String("files")     ? LabelRule::Applies::Files
                  : applies == QLatin1String("folders")   ? LabelRule::Applies::Folders
                                                          : LabelRule::Applies::Both;
        s.endGroup();
        if (r.pattern.isEmpty())
            continue;
        r.compile();
        m_rules << r;
    }
}

bool LabelRules::save() const
{
    QSettings s(m_file.isEmpty() ? Paths::configFile(QStringLiteral("labels.ini")) : m_file, QSettings::IniFormat);
    s.clear();
    int n = 1;
    for (const LabelRule &r : m_rules) {
        s.beginGroup(QString::number(n++));
        s.setValue(QStringLiteral("pattern"), r.pattern);
        if (r.color.isValid())
            s.setValue(QStringLiteral("color"), r.color.name());
        if (r.background.isValid())
            s.setValue(QStringLiteral("background"), r.background.name());
        s.setValue(QStringLiteral("bold"), r.bold);
        s.setValue(QStringLiteral("italic"), r.italic);
        s.setValue(QStringLiteral("applies"), r.applies == LabelRule::Applies::Files     ? QStringLiteral("files")
                                              : r.applies == LabelRule::Applies::Folders ? QStringLiteral("folders")
                                                                                         : QStringLiteral("both"));
        s.endGroup();
    }
    s.sync();
    return s.status() == QSettings::NoError;
}

const LabelRule *LabelRules::match(const QString &name, bool isDir) const
{
    for (const LabelRule &r : m_rules) {
        if (r.matches(name, isDir))
            return &r;
    }
    return nullptr;
}

void LabelRules::setRules(const QList<LabelRule> &rules)
{
    m_rules = rules;
    for (LabelRule &r : m_rules)
        r.compile();
}
