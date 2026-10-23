#pragma once

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

// The status bar's definition, as Opus has: text with codes in braces that
// are filled in for the list shown.
//
//   {td} {tf} {ti}        folders, files, items in the list
//   {sd} {sf} {si}        selected folders, files, items
//   {tb} {sb}             bytes of the files listed / selected
//   {folders} {files} {items} {selected}   "3 folders", "1 file"... in words
//   {hidden}              items hidden by the filter or as hidden files
//   {filter} {flat}       the filter text; "flat view" when on
//   {name} {size} {modified} {type}         the item in focus
//   {path} {folder} {tabs} {role}           folder, its name, tabs; SOURCE or DEST
//   {df} {dt} {du} {dp}   free, total, used space of the drive; percent used
//   {bar}                 a bar graph of the drive's use
//   {i:name}              an icon (folder, file, drive, info, hidden, filter, lock)
//   {|}                   a divider       {right}  what follows goes on the right
//   [ ... ]               shown only when a code in it has a value (not empty, not 0);
//                         sections can nest, and an inner one needs the outer to show
//   <b> <i> <u> <#rrggbb> and their </b> </i> </u> </#>   bold, italic, underline, color
//
// A code it does not know is left as it is written.
namespace StatusFormat {

struct Segment
{
    enum class Kind { Text, Bar, Icon };
    Kind kind = Kind::Text;
    QString html;  // Text: rich text
    QString icon;  // Icon: its name
    bool right = false;
    double fraction = 0; // Bar: 0..1 used
    bool low = false;    // Bar: nearly full
};

QString defaultDefinition(bool counts = true, bool selection = true, bool free = true);
// Ready-made definitions to start from: name and text.
QList<QPair<QString, QString>> samples();
// The codes, grouped for a menu: group, then code and what it is.
struct CodeInfo
{
    QString group;
    QString code;
    QString text;
};
QList<CodeInfo> codes();

QList<Segment> render(const QString &definition, const QHash<QString, QString> &values);
// The text without markup, left then right (for tests and tooltips).
QString plainText(const QList<Segment> &segments, bool right);
// Whether the definition is well formed: braces and brackets closed.
bool check(const QString &definition, QString *error);

} // namespace StatusFormat
