#pragma once

#include <QList>
#include <QString>
#include <QStringList>

// What every command takes, for completion in the command console, the
// command list in customize mode and the help text. Kept next to the engine;
// tst_commandengine checks the two agree.
struct ArgInfo
{
    QString name;        // NAME, or NAME= for arguments that take a value
    QStringList values;  // suggested values, empty for free text
    QString description;
};

struct CommandInfo
{
    QString name;
    QString summary;
    QList<ArgInfo> args;
    QString example;
};

// A ready-made button offered in customize mode.
struct CatalogButton
{
    QString category;
    QString label;
    QString icon;
    QString tip;
    QString left;
    QString right;
};

// A word a button's "enabled when" or "checked when" condition can use.
struct ConditionInfo
{
    QString name;
    QStringList values; // for name=value atoms
    QString description;
};

namespace CommandCatalog {

const QList<CommandInfo> &commands();
const CommandInfo *find(const QString &name); // case-insensitive
const QList<CatalogButton> &buttons();
const QList<ConditionInfo> &conditions();
QStringList conditionNames();

// One line per command, or the detail for one ("Set").
QString help(const QString &name = {});

// Completions for the word being typed at the end of line.
QStringList complete(const QString &line);

} // namespace CommandCatalog
