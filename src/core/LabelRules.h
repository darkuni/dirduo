#pragma once

#include <QColor>
#include <QList>
#include <QRegularExpression>
#include <QString>

// Labels by wildcard, Opus style: "*.mp3" in green, "TODO*" bold red.
// The first matching rule wins. Kept in ~/.config/dirduo/labels.ini:
//
//   [1]
//   pattern=*.d64;*.d81
//   color=#d97706
//   bold=true
//   applies=files        (files, folders or both)
struct LabelRule
{
    enum class Applies { Files, Folders, Both };

    QString pattern; // wildcards separated by ';'
    QColor color;
    QColor background;
    bool bold = false;
    bool italic = false;
    Applies applies = Applies::Both;

    bool matches(const QString &name, bool isDir) const;

private:
    friend class LabelRules;
    QList<QRegularExpression> m_compiled;
    void compile();
};

class LabelRules
{
public:
    // file empty means ~/.config/dirduo/labels.ini
    explicit LabelRules(const QString &file = {});

    void load();
    bool save() const;

    const LabelRule *match(const QString &name, bool isDir) const;
    QList<LabelRule> rules() const { return m_rules; }
    void setRules(const QList<LabelRule> &rules);

private:
    QString m_file;
    QList<LabelRule> m_rules;
};
