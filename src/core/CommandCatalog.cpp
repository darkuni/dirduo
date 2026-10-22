#include "core/CommandCatalog.h"

namespace {

QStringList onOff()
{
    return {QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("toggle")};
}

QList<CommandInfo> buildCommands()
{
    auto arg = [](const char *name, QStringList values, const char *desc) {
        return ArgInfo{QString::fromUtf8(name), std::move(values), QString::fromUtf8(desc)};
    };
    QList<CommandInfo> c;
    c << CommandInfo{QStringLiteral("Clipboard"), QStringLiteral("Cut, copy or paste files"),
                     {arg("COPY", {}, "Copy the selection to the clipboard"),
                      arg("CUT", {}, "Cut the selection (it moves on paste)"),
                      arg("PASTE", {}, "Paste files into the source folder; a picture or text becomes a new file"),
                      arg("AS", {}, "With PASTE of a picture or text: ask for the file name")},
                     QStringLiteral("Clipboard PASTE")};
    const QList<ArgInfo> copyArgs = {
        arg("MOVE", {}, "Move instead of copy"),
        arg("AS", {}, "Ask for a new name for each item"),
        arg("FILE=", {}, "What to copy instead of the selection"),
        arg("TO=", {}, "Where to copy instead of the destination"),
        arg("WHENEXISTS=", {QStringLiteral("ask"), QStringLiteral("replace"), QStringLiteral("newer"), QStringLiteral("skip"), QStringLiteral("rename")},
            "What to do when a name is taken"),
    };
    c << CommandInfo{QStringLiteral("Collection"), QStringLiteral("Virtual folders of links to files anywhere"),
                     {arg("ADD=", {QStringLiteral("ask")}, "Add the selection to this collection (made if new)"),
                      arg("REMOVE", {}, "Take the selection out of the open collection"),
                      arg("CREATE=", {}, "Make an empty collection"),
                      arg("DELETE=", {}, "Delete a collection (the files stay)"),
                      arg("OPEN=", {}, "Show a collection (all of them when left empty)"),
                      arg("NEWTAB", {}, "In a new tab")},
                     QStringLiteral("Collection ADD=\"C64 favourites\"")};
    c << CommandInfo{QStringLiteral("Copy"), QStringLiteral("Copy the selection to the destination"), copyArgs,
                     QStringLiteral("Copy TO=/sdcard WHENEXISTS=newer")};
    c << CommandInfo{QStringLiteral("CreateFolder"), QStringLiteral("Make new folders; several at once with commas"),
                     {arg("NAME=", {}, "Folder names, separated by commas"), arg("IN=", {}, "Where to make them")},
                     QStringLiteral("CreateFolder NAME=\"roms, bios, saves\"")};
    c << CommandInfo{QStringLiteral("Delete"), QStringLiteral("Move the selection to the trash"),
                     {arg("NORECYCLE", {}, "Delete for good instead of using the trash"),
                      arg("QUIET", {}, "Do not ask first"), arg("FILE=", {}, "What to delete instead of the selection")},
                     QStringLiteral("Delete NORECYCLE")};
    c << CommandInfo{QStringLiteral("Duplicates"), QStringLiteral("Find files that are the same and get rid of the copies"),
                     {arg("IN=", {}, "Folders to search (the source folder when left out)"),
                      arg("BY=", {QStringLiteral("contents"), QStringLiteral("namesize"), QStringLiteral("name")}, "What makes files the same"),
                      arg("MINSIZE=", {}, "Ignore files smaller than this many bytes"),
                      arg("HIDDEN", {}, "Include hidden files")},
                     QStringLiteral("Duplicates IN=/roms BY=contents")};
    c << CommandInfo{QStringLiteral("Favorite"), QStringLiteral("Favourite folders"),
                     {arg("ADD", {}, "Add the source folder (or PATH=)"),
                      arg("REMOVE", {}, "Take the source folder (or PATH=) out"),
                      arg("PATH=", {}, "Which folder"), arg("NAME=", {}, "Name shown in the menu"),
                      arg("GROUP=", {}, "Put it in this group"),
                      arg("GO=", {}, "Go to the favourite with this name"),
                      arg("ORGANISE", {}, "Rename, reorder and group favourites")},
                     QStringLiteral("Favorite ADD NAME=ROMs GROUP=Emulation")};
    c << CommandInfo{QStringLiteral("Find"), QStringLiteral("Search by name, text inside, size and date"),
                     {arg("NAME=", {}, "Wildcards (; between several) or text the name contains"),
                      arg("REGEX", {}, "NAME is a regular expression"),
                      arg("CASE", {}, "Match case"),
                      arg("CONTAINS=", {}, "Text inside the file"),
                      arg("MINSIZE=", {}, "At least, like 10M"), arg("MAXSIZE=", {}, "At most, like 700K"),
                      arg("NEWER=", {}, "Modified in the last N days, or since a date"),
                      arg("OLDER=", {}, "Not modified in the last N days, or before a date"),
                      arg("TYPE=", {QStringLiteral("files"), QStringLiteral("folders"), QStringLiteral("both")}, "What to find"),
                      arg("IN=", {}, "Where to look (the source folder when left out)"),
                      arg("HIDDEN", {}, "Include hidden files"), arg("NORECURSE", {}, "Not in subfolders"),
                      arg("COLLECTION=", {}, "Where the results go (Find Results)"),
                      arg("PRESET=", {}, "Start from a saved search"),
                      arg("RUN", {}, "Search now and show the results, without the dialog")},
                     QStringLiteral("Find NAME=*.d64 NEWER=30 IN=/roms RUN")};
    c << CommandInfo{QStringLiteral("Go"), QStringLiteral("Read a folder, or move through history"),
                     {arg("PATH=", {}, "Folder, alias (/downloads) or server (sftp://...)"),
                      arg("UP", {}, "Parent folder"), arg("BACK", {}, "Back in history"),
                      arg("FORWARD", {}, "Forward in history"), arg("REFRESH", {}, "Read the folder again"),
                      arg("SWAP", {}, "Swap the folders in the two displays"),
                      arg("NEWTAB", {}, "Open in a new tab"),
                      arg("OPENINDEST", {}, "Open in the destination display"),
                      arg("SELECT=", {}, "Select these items once the folder is read")},
                     QStringLiteral("Go /downloads NEWTAB")};
    QList<ArgInfo> moveArgs = copyArgs.mid(1);
    c << CommandInfo{QStringLiteral("Move"), QStringLiteral("Move the selection to the destination"), moveArgs,
                     QStringLiteral("Move TO=/sdcard")};
    c << CommandInfo{QStringLiteral("Print"), QStringLiteral("A listing of the folder as text, CSV or HTML"),
                     {arg("FOLDER", {}, "The source folder, as shown: columns, grouping, flat view"),
                      arg("AS=", {QStringLiteral("text"), QStringLiteral("csv"), QStringLiteral("html")}, "Format (text when left out)"),
                      arg("TO=", {QStringLiteral("clipboard"), QStringLiteral("ask")}, "Clipboard (the default), ask for a file, or a file path"),
                      arg("SELECTED", {}, "Only the selected items")},
                     QStringLiteral("Print FOLDER AS=csv TO=ask")};
    c << CommandInfo{QStringLiteral("Rename"), QStringLiteral("Rename in place, or from one name to another"),
                     {arg("FROM=", {}, "The item to rename"), arg("TO=", {}, "Its new name"),
                      arg("ADVANCED", {}, "Open Advanced Rename on the selection"),
                      arg("UNDO", {}, "Undo the last batch rename"),
                      arg("OLDNAME=", {}, "Wildcard, text to find, or regular expression"),
                      arg("NEWNAME=", {}, "New name: * matches, # or [#] numbers, [date] [mdate] [name] [ext] [parent]"),
                      arg("MODE=", {QStringLiteral("wildcard"), QStringLiteral("findreplace"), QStringLiteral("regexp")}, "How OLDNAME is read"),
                      arg("CASE=", {QStringLiteral("lower"), QStringLiteral("upper"), QStringLiteral("title"), QStringLiteral("sentence")}, "Change the name's case"),
                      arg("EXTCASE=", {QStringLiteral("lower"), QStringLiteral("upper")}, "Change the extension's case"),
                      arg("IGNOREEXT", {}, "Leave the extension alone"),
                      arg("CASESENSITIVE", {}, "Match case exactly"),
                      arg("START=", {}, "First number (1)"), arg("STEP=", {}, "Number step (1)"),
                      arg("WIDTH=", {}, "Pad numbers to this many digits"),
                      arg("TRIM", {}, "No leading, trailing or doubled spaces"),
                      arg("PRESET=", {}, "Start from a saved preset")},
                     QStringLiteral("Rename OLDNAME=*.jpg NEWNAME=\"Holiday ###.jpg\"")};
    c << CommandInfo{QStringLiteral("Select"), QStringLiteral("Change the selection"),
                     {arg("ALL", {}, "Select everything"), arg("NONE", {}, "Select nothing"),
                      arg("INVERT", {}, "Swap selected and unselected"),
                      arg("PATTERN=", {}, "Select names matching a wildcard")},
                     QStringLiteral("Select PATTERN=*.d64")};
    c << CommandInfo{
        QStringLiteral("Set"),
        QStringLiteral("Change how the lister looks and behaves"),
        {arg("DUAL=", {QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("toggle"), QStringLiteral("vertical"), QStringLiteral("horizontal"), QStringLiteral("flip")},
             "One display or two, side by side or stacked"),
         arg("SOURCE=", {QStringLiteral("toggle")}, "Make the other display the source"),
         arg("DESTLOCK=", onOff(), "Keep the destination where it is"),
         arg("NAVLOCK=", onOff(), "The other display follows into and out of folders"),
         arg("VIEW=", {QStringLiteral("details"), QStringLiteral("list"), QStringLiteral("icons")}, "View mode"),
         arg("HIDDEN=", onOff(), "Show hidden files"),
         arg("THEME=", {QStringLiteral("next")}, "Theme by name, or the next one"),
         arg("SIZE=", {QStringLiteral("normal"), QStringLiteral("large"), QStringLiteral("larger"), QStringLiteral("bigger"), QStringLiteral("smaller")},
             "Text and icon size, for small screens"),
         arg("GROUPBY=", {QStringLiteral("none"), QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("modified"), QStringLiteral("extension"), QStringLiteral("folder")},
             "Group the list"),
         arg("GROUPCOLLAPSE=", {QStringLiteral("all"), QStringLiteral("none"), QStringLiteral("toggle")}, "Fold or open groups"),
         arg("FILTERBAR=", onOff(), "Show the filter bar"),
         arg("FILTER=", {}, "Show only matching names (a leading ! hides them instead)"),
         arg("CHECKBOXES=", onOff(), "A check box on each row"),
         arg("FOLDERSIZES=", onOff(), "Calculate every folder's size"),
         arg("FLATVIEW=", {QStringLiteral("off"), QStringLiteral("grouped"), QStringLiteral("mixed"), QStringLiteral("nofolders"), QStringLiteral("toggle")},
             "Everything under the folder in one list"),
         arg("TREE=", {QStringLiteral("off"), QStringLiteral("shared"), QStringLiteral("separate"), QStringLiteral("toggle")}, "Folder tree"),
         arg("FORMAT=", {QStringLiteral("save"), QStringLiteral("savesub"), QStringLiteral("clear"), QStringLiteral("savedefault"), QStringLiteral("cleardefault")},
             "Save or clear this folder's format"),
         arg("AUTOFORMAT=", onOff(), "Icons for folders of pictures"),
         arg("COLUMNSTOGGLE=", {QStringLiteral("created"), QStringLiteral("accessed"), QStringLiteral("extension"), QStringLiteral("permissions"), QStringLiteral("owner"), QStringLiteral("group")},
             "Show or hide a column")},
        QStringLiteral("Set DUAL=vertical VIEW=details")};
    c << CommandInfo{QStringLiteral("Settings"), QStringLiteral("Preferences, and backing up or restoring every setting"),
                     {arg("BACKUP=", {QStringLiteral("ask")}, "Save every setting to one zip (ask: choose where)"),
                      arg("RESTORE=", {QStringLiteral("ask")}, "Put back settings from a backup zip"),
                      arg("QUIET", {}, "Restore without asking first"),
                      arg("PAGE=", {QStringLiteral("appearance"), QStringLiteral("theme"), QStringLiteral("labels"), QStringLiteral("aliases"),
                                    QStringLiteral("formats"), QStringLiteral("toolbars")},
                          "Open Preferences on this page (no arguments: Preferences)")},
                     QStringLiteral("Settings PAGE=theme")};
    c << CommandInfo{QStringLiteral("Sync"), QStringLiteral("Compare two folders and make them match"),
                     {arg("LEFT=", {}, "One folder (the source when left out)"),
                      arg("RIGHT=", {}, "The other (the destination when left out)"),
                      arg("MODE=", {QStringLiteral("update"), QStringLiteral("mirror"), QStringLiteral("twoway")},
                          "Update the right, mirror to it, or both ways"),
                      arg("COMPARE=", {QStringLiteral("datesize"), QStringLiteral("date"), QStringLiteral("size"), QStringLiteral("contents")},
                          "How files are compared"),
                      arg("EXCLUDE=", {}, "Names to leave out, ; between them"),
                      arg("RUN", {}, "Do it without showing the comparison")},
                     QStringLiteral("Sync LEFT=/roms RIGHT=/sdcard/roms MODE=update")};
    c << CommandInfo{QStringLiteral("Toolbar"), QStringLiteral("Place, show or customize toolbars"),
                     {arg("NAME=", {}, "Which toolbar (buttons, drives, or one you made)"),
                      arg("PLACE=", {QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("between")},
                          "Where it goes"),
                      arg("NEWROW", {}, "Start a new row instead of sharing one"),
                      arg("STATE=", onOff(), "Show or hide it"),
                      arg("CUSTOMIZE", {}, "Enter customize mode")},
                     QStringLiteral("Toolbar NAME=drives PLACE=between")};
    return c;
}

QList<CatalogButton> buildButtons()
{
    auto b = [](const char *cat, const char *label, const char *icon, const char *tip, const char *left, const char *right = "") {
        return CatalogButton{QString::fromUtf8(cat), QString::fromUtf8(label), QString::fromUtf8(icon),
                             QString::fromUtf8(tip), QString::fromUtf8(left), QString::fromUtf8(right)};
    };
    return {
        b("File operations", "Copy", "edit-copy", "Copy the selection to the destination", "Copy", "Copy AS"),
        b("File operations", "Move", "transform-move", "Move the selection to the destination", "Copy MOVE", "Copy MOVE AS"),
        b("File operations", "Delete", "edit-delete", "Move the selection to the trash", "Delete", "Delete NORECYCLE"),
        b("File operations", "Rename", "edit-rename", "Rename in place", "Rename"),
        b("File operations", "Advanced Rename", "edit-rename", "Rename many items at once, with a preview", "Rename ADVANCED"),
        b("File operations", "Undo Rename", "edit-undo", "Undo the last batch rename", "Rename UNDO"),
        b("File operations", "New Folder", "folder-new", "Create folders", "CreateFolder"),
        b("File operations", "Copy As", "edit-copy", "Copy with a new name", "Copy AS"),
        b("File operations", "Copy If Newer", "edit-copy", "Copy, replacing only older files", "Copy WHENEXISTS=newer"),
        b("Clipboard", "Cut", "edit-cut", "Cut the selection", "Clipboard CUT"),
        b("Clipboard", "Copy to Clipboard", "edit-copy", "Copy the selection to the clipboard", "Clipboard COPY"),
        b("Clipboard", "Paste", "edit-paste", "Paste into the source", "Clipboard PASTE"),
        b("Clipboard", "Paste as File", "edit-paste", "Paste a picture or text as a file you name", "Clipboard PASTE AS"),
        b("Folder", "Synchronize", "folder-sync", "Compare the source and destination and make them match", "Sync"),
        b("Folder", "Duplicates", "edit-copy", "Find duplicate files under the source folder", "Duplicates"),
        b("Folder", "Print Folder", "document-print", "Copy a listing of the folder to the clipboard", "Print FOLDER", "Print FOLDER AS=html TO=ask"),
        b("Folder", "Export CSV", "document-export", "Save a listing of the folder as CSV", "Print FOLDER AS=csv TO=ask"),
        b("Navigation", "Back", "go-previous", "Back", "Go BACK"),
        b("Navigation", "Forward", "go-next", "Forward", "Go FORWARD"),
        b("Navigation", "Up", "go-up", "Parent folder", "Go UP"),
        b("Navigation", "Home", "go-home", "Home folder", "Go ~"),
        b("Navigation", "Refresh", "view-refresh", "Read the folder again", "Go REFRESH"),
        b("Navigation", "Swap", "view-split-left-right", "Swap the two displays", "Go SWAP"),
        b("Navigation", "Same", "go-jump", "Open the source folder in the destination", "Go {sourcepath} OPENINDEST"),
        b("Navigation", "New Tab", "tab-new", "Open the source folder in a new tab", "Go {sourcepath} NEWTAB"),
        b("Layout", "Vertical", "view-split-left-right", "Displays side by side", "Set DUAL=vertical", "Set DUAL=off"),
        b("Layout", "Horizontal", "view-split-top-bottom", "Displays top and bottom", "Set DUAL=horizontal", "Set DUAL=off"),
        b("Layout", "Single", "view-file-columns", "One display", "Set DUAL=off"),
        b("Layout", "Lock Dest", "object-locked", "Keep the destination where it is", "Set DESTLOCK=toggle"),
        b("Layout", "Nav Lock", "object-locked", "The other display follows", "Set NAVLOCK=toggle"),
        b("Layout", "Tree", "view-list-tree", "Folder tree", "Set TREE=toggle", "Set TREE=separate"),
        b("View", "Details", "view-list-details", "Details view", "Set VIEW=details"),
        b("View", "List", "view-list-text", "List view", "Set VIEW=list"),
        b("View", "Icons", "view-list-icons", "Icon view", "Set VIEW=icons"),
        b("View", "Hidden", "view-hidden", "Show hidden files", "Set HIDDEN=toggle"),
        b("View", "Flat", "view-list-details", "Flat view", "Set FLATVIEW=toggle", "Set FLATVIEW=mixed"),
        b("View", "Group", "view-group", "Group by type", "Set GROUPBY=type", "Set GROUPBY=none"),
        b("View", "Filter", "view-filter", "Filter bar", "Set FILTERBAR=toggle"),
        b("View", "Check Boxes", "checkbox", "A check box per row", "Set CHECKBOXES=toggle"),
        b("View", "Sizes", "folder-stats", "Calculate every folder's size", "Set FOLDERSIZES=toggle"),
        b("View", "Theme", "color-management", "Next theme", "Set THEME=next"),
        CatalogButton{QStringLiteral("Favourites"), QStringLiteral("Favourites"), QStringLiteral("bookmarks"),
                      QStringLiteral("Your favourite folders"), {}, {}, QStringLiteral("favorites")},
        CatalogButton{QStringLiteral("Favourites"), QStringLiteral("Frequent"), QStringLiteral("document-open-recent"),
                      QStringLiteral("The folders you use most"), {}, {}, QStringLiteral("frequent")},
        CatalogButton{QStringLiteral("Favourites"), QStringLiteral("Aliases"), QStringLiteral("folder-bookmark"),
                      QStringLiteral("Every alias"), {}, {}, QStringLiteral("aliases")},
        CatalogButton{QStringLiteral("Favourites"), QStringLiteral("Collections"), QStringLiteral("folder-favorites"),
                      QStringLiteral("Every collection"), {}, {}, QStringLiteral("collections")},
        b("Favourites", "Add Favourite", "bookmark-new", "Add the source folder to Favourites", "Favorite ADD", "Favorite ORGANISE"),
        b("Selection", "Select All", "edit-select-all", "Select everything", "Select ALL"),
        b("Selection", "Select None", "edit-select-none", "Select nothing", "Select NONE"),
        b("Selection", "Invert", "edit-select-invert", "Invert the selection", "Select INVERT"),
        b("Selection", "Select Pattern", "edit-select", "Select by wildcard",
          "Select PATTERN={dlgstring|Select files matching (for example *.mp3):}"),
    };
}

QList<ConditionInfo> buildConditions()
{
    auto c = [](const char *name, QStringList values, const char *desc) {
        return ConditionInfo{QString::fromUtf8(name), std::move(values), QString::fromUtf8(desc)};
    };
    return {
        c("selection", {}, "Something is selected"),
        c("files", {}, "At least one file is selected"),
        c("folders", {}, "At least one folder is selected"),
        c("single", {}, "Exactly one item is selected"),
        c("multiple", {}, "Two or more items are selected"),
        c("dual", {}, "Two displays are showing"),
        c("dest", {}, "There is a destination folder"),
        c("destlocked", {}, "The destination is locked"),
        c("navlock", {}, "Navigation lock is on"),
        c("remote", {}, "The source is on a server or in an archive"),
        c("canup", {}, "The source has a parent folder"),
        c("favorite", {}, "The source folder is a favourite"),
        c("back", {}, "There is history to go back to"),
        c("forward", {}, "There is history to go forward to"),
        c("clipboard", {}, "There are files on the clipboard"),
        c("hidden", {}, "Hidden files are showing"),
        c("tree", {}, "A folder tree is showing"),
        c("flat", {}, "Flat view is on"),
        c("grouped", {}, "The list is grouped"),
        c("filter", {}, "The filter bar is open"),
        c("checkboxes", {}, "Check boxes are on"),
        c("foldersizes", {}, "Every folder's size is being calculated"),
        c("view", {QStringLiteral("details"), QStringLiteral("list"), QStringLiteral("icons")}, "The view mode"),
        c("layout", {QStringLiteral("single"), QStringLiteral("vertical"), QStringLiteral("horizontal")}, "The display layout"),
        c("groupby", {QStringLiteral("none"), QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("modified"), QStringLiteral("extension"), QStringLiteral("folder")},
          "What the list is grouped by"),
        c("treemode", {QStringLiteral("off"), QStringLiteral("shared"), QStringLiteral("separate")}, "Which folder tree is showing"),
        c("theme", {}, "The theme, by name (spaces removed, for example linopusdark)"),
    };
}

} // namespace

namespace CommandCatalog {

const QList<CommandInfo> &commands()
{
    static const QList<CommandInfo> list = buildCommands();
    return list;
}

const CommandInfo *find(const QString &name)
{
    for (const CommandInfo &c : commands()) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0)
            return &c;
    }
    return nullptr;
}

const QList<CatalogButton> &buttons()
{
    static const QList<CatalogButton> list = buildButtons();
    return list;
}

const QList<ConditionInfo> &conditions()
{
    static const QList<ConditionInfo> list = buildConditions();
    return list;
}

QStringList conditionNames()
{
    QStringList out;
    for (const ConditionInfo &c : conditions())
        out << c.name;
    return out;
}

QString help(const QString &name)
{
    if (name.isEmpty()) {
        QStringList lines;
        for (const CommandInfo &c : commands())
            lines << QStringLiteral("%1 - %2").arg(c.name, -13).arg(c.summary);
        lines << QString() << QStringLiteral("help <command> for its arguments. Codes: {sourcepath} {destpath} {filepath} {file} {focusedpath} {dlgstring|prompt}");
        return lines.join(QLatin1Char('\n'));
    }
    const CommandInfo *c = find(name);
    if (!c)
        return QStringLiteral("There is no command called %1.").arg(name);
    QStringList lines{QStringLiteral("%1 - %2").arg(c->name, c->summary)};
    for (const ArgInfo &a : c->args) {
        QString line = QStringLiteral("  %1").arg(a.name, -16) + a.description;
        if (!a.values.isEmpty())
            line += QStringLiteral(" (%1)").arg(a.values.join(QStringLiteral(", ")));
        lines << line;
    }
    lines << QStringLiteral("Example: %1").arg(c->example);
    return lines.join(QLatin1Char('\n'));
}

QStringList complete(const QString &line)
{
    // The word under the cursor is the last one; quotes are not completed.
    const int lastSpace = line.lastIndexOf(QLatin1Char(' '));
    const QString word = line.mid(lastSpace + 1);
    const QString head = line.left(lastSpace + 1);
    QStringList out;
    if (lastSpace < 0) {
        for (const CommandInfo &c : commands()) {
            if (c.name.startsWith(word, Qt::CaseInsensitive))
                out << c.name;
        }
        if (QStringLiteral("help").startsWith(word, Qt::CaseInsensitive))
            out << QStringLiteral("help");
        return out;
    }
    const QString cmdName = line.section(QLatin1Char(' '), 0, 0);
    if (cmdName.compare(QLatin1String("help"), Qt::CaseInsensitive) == 0) {
        for (const CommandInfo &c : commands()) {
            if (c.name.startsWith(word, Qt::CaseInsensitive))
                out << head + c.name;
        }
        return out;
    }
    const CommandInfo *c = find(cmdName);
    if (!c)
        return out;
    const int eq = word.indexOf(QLatin1Char('='));
    if (eq >= 0) {
        // Value of NAME=: the argument's suggested values.
        const QString key = word.left(eq + 1).toUpper();
        const QString part = word.mid(eq + 1);
        for (const ArgInfo &a : c->args) {
            if (a.name != key)
                continue;
            for (const QString &v : a.values) {
                if (v.startsWith(part, Qt::CaseInsensitive))
                    out << head + key + v;
            }
        }
        return out;
    }
    for (const ArgInfo &a : c->args) {
        if (a.name.startsWith(word, Qt::CaseInsensitive))
            out << head + a.name;
    }
    return out;
}

} // namespace CommandCatalog
