#include "core/CommandCatalog.h"

#include <algorithm>

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
    c << CommandInfo{QStringLiteral("Archive"), QStringLiteral("Make zip, 7z and tar archives; extract those, RAR, ISO and more"),
                     {arg("CREATE", {}, "Put the selection in a new archive (asks for the name and format unless NAME= is given)"),
                      arg("NAME=", {}, "The archive's file name; its extension picks the format"),
                      arg("QUICK", {}, "No questions: named after the item (or the folder, for several)"),
                      arg("FORMAT=", {QStringLiteral("zip"), QStringLiteral("7z"), QStringLiteral("tar"), QStringLiteral("tar.gz"),
                                      QStringLiteral("tar.xz"), QStringLiteral("tar.bz2"), QStringLiteral("tar.zst")},
                          "Archive format"),
                      arg("EXTRACT", {}, "Extract the selected archives"),
                      arg("MODE=", {QStringLiteral("smart"), QStringLiteral("folder"), QStringLiteral("here")},
                          "smart: a folder named after the archive unless it holds just one folder; folder: always; here: straight in"),
                      arg("TRASH", {}, "Extract: then move each archive that came out completely to the trash"),
                      arg("DEST", {}, "Into the destination folder (otherwise the source folder)"),
                      arg("TO=", {}, "Into this folder"),
                      arg("WHENEXISTS=", {QStringLiteral("rename"), QStringLiteral("replace"), QStringLiteral("skip")},
                          "When a name is taken (rename keeps both)"),
                      arg("PASSWORD=", {}, "Extract: for encrypted archives (asked for otherwise). Create: encrypt a zip with AES-256"),
                      arg("FILE=", {}, "What to use instead of the selection")},
                     QStringLiteral("Archive EXTRACT DEST")};
    c << CommandInfo{QStringLiteral("Server"), QStringLiteral("The server address book: SFTP, FTP, SMB and WebDAV places by name"),
                     {arg("GO=", {}, "Open the server with this name"),
                      arg("NEWTAB", {}, "With GO: in a new tab"),
                      arg("OPENINDEST", {}, "With GO: in the destination"),
                      arg("ADD", {}, "Add a server (asks for the details unless URL= is given)"),
                      arg("URL=", {}, "With ADD: the address, e.g. sftp://deck@nas/media"),
                      arg("NAME=", {}, "With ADD: its name"),
                      arg("REMOVE=", {}, "Remove the server with this name")},
                     QStringLiteral("Server GO=NAS")};
    c << CommandInfo{QStringLiteral("OpenWith"), QStringLiteral("Open the selected files with an application"),
                     {arg("APP=", {}, "The application: its name (Kate) or desktop file (org.kde.kate.desktop); asks when left out"),
                      arg("DEFAULT", {}, "Also make it DirDuo's default for this type of file"),
                      arg("FILE=", {}, "What to open instead of the selection")},
                     QStringLiteral("OpenWith APP=Kate")};
    c << CommandInfo{QStringLiteral("Checksum"), QStringLiteral("CRC32, MD5, SHA-1 or SHA-256 of the selection (folders walked), to copy or save as a list; or check files against .md5, .sha1, .sha256 and .sfv lists"),
                     {arg("ALGO=", {QStringLiteral("md5"), QStringLiteral("sha1"), QStringLiteral("sha256"), QStringLiteral("crc32")}, "Which checksum (MD5 unless given)"),
                      arg("CLIPBOARD", {}, "Copy them when done, without showing the window"),
                      arg("SAVE=", {QStringLiteral("ask")}, "Save a list when done (no value: next to the files, named after them)"),
                      arg("VERIFY", {}, "Check the files named in the selected lists"),
                      arg("FILE=", {}, "Files to use instead of the selection")},
                     QStringLiteral("Checksum ALGO=sha256")};
    c << CommandInfo{QStringLiteral("Clean"), QStringLiteral("Move .DS_Store, ._ files, Thumbs.db, desktop.ini and __MACOSX under a folder to the trash"),
                     {arg("FILE=", {}, "Folder to clean (else the selected folders, else the source)"),
                      arg("QUIET", {}, "No question")},
                     QStringLiteral("Clean")};
    c << CommandInfo{QStringLiteral("Split"), QStringLiteral("Cut a file into pieces (name.001, name.002...), for a FAT32 card or a size limit"),
                     {arg("SIZE=", {QStringLiteral("fat32"), QStringLiteral("100M"), QStringLiteral("1G"), QStringLiteral("2G"), QStringLiteral("cd"), QStringLiteral("dvd")},
                          "Size of each piece (asks when not given)"),
                      arg("PIECES=", {}, "How many pieces instead of a size"),
                      arg("TO=", {QStringLiteral("dest")}, "Where the pieces go (else beside the file)"),
                      arg("WHENEXISTS=", {QStringLiteral("replace"), QStringLiteral("skip")}, "When pieces are already there"),
                      arg("FILE=", {}, "File to split instead of the selection")},
                     QStringLiteral("Split SIZE=fat32")};
    c << CommandInfo{QStringLiteral("Join"), QStringLiteral("Put pieces back together; selecting name.001 finds the rest"),
                     {arg("NAME=", {}, "Name of the joined file (else the pieces' name without .001)"),
                      arg("TO=", {QStringLiteral("dest")}, "Where it goes (else beside the pieces)"),
                      arg("WHENEXISTS=", {QStringLiteral("replace"), QStringLiteral("skip"), QStringLiteral("rename")}, "When the file is already there"),
                      arg("FILE=", {}, "Pieces instead of the selection")},
                     QStringLiteral("Join")};
    c << CommandInfo{QStringLiteral("Steam"), QStringLiteral("Add the selected programs to Steam as non-Steam games (Windows ones run through Proton)"),
                     {arg("ADD", {}, "Add them (Steam must be running)"), arg("FILE=", {}, "Programs to add instead of the selection")},
                     QStringLiteral("Steam ADD")};
    c << CommandInfo{QStringLiteral("Show"), QStringLiteral("The viewer: pictures, music, video, text and more in their own window, stepping through the folder"),
                     {arg("FILE=", {}, "File (or folder) to show instead of the selection"),
                      arg("AUTOFILELIST=", {QStringLiteral("all")}, "Next/Previous go through the files shown in the tab (all: every file in the folder)"),
                      arg("LISTSIBLINGS", {}, "Next/Previous go through the folder's files of the same kind"),
                      arg("FULLSCREEN", {}, "Open in full screen"),
                      arg("SLIDESHOW", {}, "Start a slideshow"),
                      arg("LISTERLINK=", {QStringLiteral("close"), QStringLiteral("toggle")}, "Show the file in focus in the lister (close: close the linked viewer)"),
                      arg("USEEXISTING=", {QStringLiteral("no")}, "no: a new window even when one is open"),
                      arg("POS=", {}, "Where the window goes: x,y"),
                      arg("SIZE=", {}, "The window's size: width,height"),
                      arg("VIEWERCMD=", {QStringLiteral("next"), QStringLiteral("prev"), QStringLiteral("first"), QStringLiteral("last"),
                                         QStringLiteral("nextrandom"), QStringLiteral("zoom,fit"), QStringLiteral("zoom,grow"), QStringLiteral("zoom,reset"),
                                         QStringLiteral("rotate,left"), QStringLiteral("rotate,right"), QStringLiteral("flip,h"), QStringLiteral("flip,v"),
                                         QStringLiteral("fullscreen"), QStringLiteral("slideshow"), QStringLiteral("info"), QStringLiteral("hex"),
                                         QStringLiteral("mark"), QStringLiteral("selectfile"), QStringLiteral("playpause"), QStringLiteral("close")},
                          "A command for the open viewer")},
                     QStringLiteral("Show")};
    c << CommandInfo{QStringLiteral("Tag"), QStringLiteral("Color labels, star ratings, tags and descriptions (the Tags window when given nothing)"),
                     {arg("LABEL=", {QStringLiteral("Red"), QStringLiteral("Orange"), QStringLiteral("Yellow"), QStringLiteral("Green"),
                                     QStringLiteral("Blue"), QStringLiteral("Purple"), QStringLiteral("Gray"), QStringLiteral("none")},
                          "Color label (Preferences > Labels lists them)"),
                      arg("RATING=", {QStringLiteral("0"), QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4"),
                                      QStringLiteral("5"), QStringLiteral("+"), QStringLiteral("-")},
                          "Stars, 0 to 5; + and - one more or less"),
                      arg("ADD=", {}, "Add tags (comma between several)"),
                      arg("REMOVE=", {}, "Remove tags"),
                      arg("SET=", {}, "Replace the tags"),
                      arg("CLEAR", {}, "Remove every tag"),
                      arg("COMMENT=", {}, "The description"),
                      arg("CLEARALL", {}, "Remove label, rating, tags and description"),
                      arg("FILE=", {}, "What to tag instead of the selection")},
                     QStringLiteral("Tag RATING=4 ADD=favorite")};
    c << CommandInfo{QStringLiteral("Terminal"), QStringLiteral("Open a terminal window in the folder (Konsole, or another one installed)"),
                     {arg("PATH=", {}, "Folder to open in (otherwise the source folder)"),
                      arg("PROGRAM=", {QStringLiteral("konsole"), QStringLiteral("gnome-terminal"), QStringLiteral("alacritty"), QStringLiteral("kitty")},
                          "Terminal to use first")},
                     QStringLiteral("Terminal")};
    c << CommandInfo{QStringLiteral("MakeLink"), QStringLiteral("Make symbolic links to the selected items"),
                     {arg("DEST", {}, "In the destination folder (otherwise beside each item, as \"Name (link)\")"),
                      arg("TO=", {}, "In this folder"),
                      arg("NAME=", {}, "The link's name (one item)"),
                      arg("RELATIVE", {}, "Point to the item by a relative path, so both can move together"),
                      arg("FILE=", {}, "What to link to instead of the selection")},
                     QStringLiteral("MakeLink DEST RELATIVE")};
    c << CommandInfo{QStringLiteral("Properties"), QStringLiteral("Size, link target, dates and permissions of the selection"),
                     {arg("FILE=", {}, "Show these instead of the selection")},
                     QStringLiteral("Properties")};
    c << CommandInfo{QStringLiteral("SetAttr"), QStringLiteral("Change dates and permissions (the Properties window when given nothing to change)"),
                     {arg("MODIFIED=", {QStringLiteral("now")}, "Modified date: 2024-05-01 13:30, or now"),
                      arg("ACCESSED=", {QStringLiteral("now")}, "Accessed date"),
                      arg("PERMS=", {QStringLiteral("644"), QStringLiteral("755"), QStringLiteral("u+x"), QStringLiteral("go-w")},
                          "Permissions: 644, rwxr-xr-x, or changes like u+x,go-w"),
                      arg("RECURSE", {}, "Everything inside folders too"),
                      arg("FILE=", {}, "What to change instead of the selection")},
                     QStringLiteral("SetAttr MODIFIED=now RECURSE")};
    c << CommandInfo{QStringLiteral("Clipboard"), QStringLiteral("Cut, copy or paste files"),
                     {arg("COPY", {}, "Copy the selection to the clipboard"),
                      arg("CUT", {}, "Cut the selection (it moves on paste)"),
                      arg("PASTE", {}, "Paste files into the source folder; a picture or text becomes a new file"),
                      arg("AS", {}, "With PASTE of a picture or text: ask for the file name"),
                      arg("STRUCTURE=", {QStringLiteral("ask"), QStringLiteral("keep"), QStringLiteral("flat")},
                          "With PASTE of items copied from a flat view: keep their folders, or put them all in one folder")},
                     QStringLiteral("Clipboard PASTE")};
    const QList<ArgInfo> copyArgs = {
        arg("MOVE", {}, "Move instead of copy"),
        arg("AS", {}, "Ask for a new name for each item"),
        arg("FILE=", {}, "What to copy instead of the selection"),
        arg("TO=", {QStringLiteral("ask")}, "Where to copy instead of the destination (ask: choose a folder)"),
        arg("WHENEXISTS=", {QStringLiteral("ask"), QStringLiteral("replace"), QStringLiteral("newer"), QStringLiteral("skip"), QStringLiteral("rename")},
            "What to do when a name is taken"),
        arg("STRUCTURE=", {QStringLiteral("ask"), QStringLiteral("keep"), QStringLiteral("flat")},
            "From a flat view: keep the folders items came from, or put them all in one folder"),
    };
    c << CommandInfo{QStringLiteral("Collection"), QStringLiteral("Virtual folders of links to files anywhere"),
                     {arg("ADD=", {QStringLiteral("ask")}, "Add the selection to this collection (made if new)"),
                      arg("REMOVE", {}, "Take the selection out of the open collection"),
                      arg("CREATE=", {}, "Make an empty collection"),
                      arg("DELETE=", {}, "Delete a collection (the files stay)"),
                      arg("OPEN=", {}, "Show a collection (all of them when left empty)"),
                      arg("NEWTAB", {}, "In a new tab")},
                     QStringLiteral("Collection ADD=\"C64 favorites\"")};
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
    c << CommandInfo{QStringLiteral("Favorite"), QStringLiteral("Favorite folders"),
                     {arg("ADD", {}, "Add the source folder (or PATH=)"),
                      arg("REMOVE", {}, "Take the source folder (or PATH=) out"),
                      arg("PATH=", {}, "Which folder"), arg("NAME=", {}, "Name shown in the menu"),
                      arg("GROUP=", {}, "Put it in this group"),
                      arg("GO=", {}, "Go to the favorite with this name"),
                      arg("ORGANIZE", {}, "Rename, reorder and group favorites")},
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
                      arg("SELECT=", {}, "Select these items once the folder is read"),
                      arg("TABGROUPLOAD=", {}, "Open the tabs of this tab group"),
                      arg("TABGROUPSAVE=", {}, "Save the open tabs as a tab group with this name (asks when none is given)")},
                     QStringLiteral("Go /downloads NEWTAB")};
    c << CommandInfo{QStringLiteral("Image"), QStringLiteral("Convert, resize, turn and flip pictures"),
                     {arg("CONVERT=", {QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("webp"), QStringLiteral("bmp"), QStringLiteral("keep")},
                          "Format to save as (keep: each picture's own)"),
                      arg("QUALITY=", {}, "1 to 100, for JPEG and WebP"),
                      arg("WIDTH=", {}, "New width in pixels (height follows unless HEIGHT is given too)"),
                      arg("HEIGHT=", {}, "New height in pixels (with WIDTH: fit inside both)"),
                      arg("PERCENT=", {}, "Resize to this percentage"),
                      arg("STRETCH", {}, "With WIDTH and HEIGHT: exactly that size, not fitted"),
                      arg("ENLARGE", {}, "Allow making pictures bigger"),
                      arg("ROTATE=", {QStringLiteral("90"), QStringLiteral("180"), QStringLiteral("270")}, "Turn clockwise by this many degrees"),
                      arg("FLIP=", {QStringLiteral("h"), QStringLiteral("v"), QStringLiteral("both")}, "Flip left to right (h) or upside down (v)"),
                      arg("TO=", {QStringLiteral("dest")}, "Folder to save in (next to each picture when left out)"),
                      arg("WHENEXISTS=", {QStringLiteral("rename"), QStringLiteral("replace"), QStringLiteral("skip")}, "When a file of that name is there"),
                      arg("FILE=", {}, "Pictures to convert instead of the selection"),
                      arg("RUN", {}, "Convert straight away, without the window")},
                     QStringLiteral("Image CONVERT=jpg WIDTH=1280 TO=dest RUN")};
    QList<ArgInfo> moveArgs = copyArgs.mid(1);
    c << CommandInfo{QStringLiteral("Layout"), QStringLiteral("Save and switch whole lister arrangements"),
                     {arg("SAVE=", {QStringLiteral("ask")}, "Save this lister's arrangement under a name"),
                      arg("FOLDERS", {}, "With SAVE: the folders and tabs too (otherwise loading keeps yours)"),
                      arg("LOAD=", {}, "Switch to a saved layout"),
                      arg("DELETE=", {}, "Delete a saved layout")},
                     QStringLiteral("Layout LOAD=\"ROM sorting\"")};
    c << CommandInfo{QStringLiteral("Metadata"), QStringLiteral("Edit music tags and photo details of the selection"),
                     {arg("TITLE=", {}, "Title (empty clears it)"), arg("ARTIST=", {}, "Artist (photos: photographer)"),
                      arg("ALBUMARTIST=", {}, "Album artist"), arg("ALBUM=", {}, "Album"), arg("GENRE=", {}, "Genre"),
                      arg("COMMENT=", {}, "Comment"), arg("YEAR=", {}, "Year"),
                      arg("TRACK=", {QStringLiteral("auto")}, "Track number; auto numbers the files in list order"),
                      arg("START=", {}, "With TRACK=auto: the first number"), arg("DISC=", {}, "Disc number"),
                      arg("TAKEN=", {}, "Photos: date taken, like 2026-10-21 18:15"),
                      arg("SHIFT=", {QStringLiteral("+1h"), QStringLiteral("-1h")}, "Photos: move each date taken by this much"),
                      arg("DESCRIPTION=", {}, "Photos: description"), arg("COPYRIGHT=", {}, "Copyright"),
                      arg("COPY", {}, "Remember the tags of the file in focus"),
                      arg("PASTE", {}, "Write the remembered tags (artist, album, genre, year, comment) to the selection"),
                      arg("ALL", {}, "With PASTE: titles, track numbers and dates too"),
                      arg("FILE=", {}, "Files to change instead of the selection"),
                      arg("PANE=", {QStringLiteral("toggle"), QStringLiteral("on"), QStringLiteral("off")}, "The Metadata pane")},
                     QStringLiteral("Metadata ALBUM=\"Passenger Seat Radio\" GENRE=Podcast TRACK=auto")};
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
    c << CommandInfo{QStringLiteral("Script"), QStringLiteral("JavaScript scripts in ~/.config/dirduo/scripts: they add commands, columns and event handlers"),
                     {arg("RELOAD", {}, "Load every script again (saving one does this by itself)"),
                      arg("NEW=", {}, "Make a new script from the template and open it"),
                      arg("EXAMPLES", {}, "Add the example scripts (ROM Region, Playlists, Recent Folders)"),
                      arg("FOLDER", {}, "Open the scripts folder")},
                     QStringLiteral("Script NEW=\"ROM tools\"")};
    c << CommandInfo{QStringLiteral("Select"), QStringLiteral("Change the selection"),
                     {arg("ALL", {}, "Select everything"), arg("NONE", {}, "Select nothing"),
                      arg("INVERT", {}, "Swap selected and unselected"),
                      arg("PATTERN=", {}, "Names matching wildcards (; between several)"),
                      arg("COMPARE=", {QStringLiteral("newerormissing"), QStringLiteral("missing"), QStringLiteral("newer"),
                                       QStringLiteral("older"), QStringLiteral("same"), QStringLiteral("different")},
                          "Against the same name in the other display"),
                      arg("BOTH", {}, "With COMPARE: select them in the other display too"),
                      arg("KIND=", {QStringLiteral("image"), QStringLiteral("audio"), QStringLiteral("video"), QStringLiteral("archive"),
                                    QStringLiteral("diskimage"), QStringLiteral("document"), QStringLiteral("code"), QStringLiteral("folder")},
                          "Kind of file"),
                      arg("TYPE=", {QStringLiteral("files"), QStringLiteral("folders"), QStringLiteral("both")}, "Files, folders or both"),
                      arg("MINSIZE=", {}, "At least, like 10M"), arg("MAXSIZE=", {}, "At most, like 700K"),
                      arg("NEWER=", {}, "Modified in the last N days, or since a date"),
                      arg("OLDER=", {}, "Not modified in the last N days, or before a date"),
                      arg("ADD", {}, "Add to the selection instead of replacing it"),
                      arg("DESELECT", {}, "Take matching items out of the selection"),
                      arg("DIALOG", {}, "Open the Select window")},
                     QStringLiteral("Select COMPARE=newerormissing")};
    c << CommandInfo{
        QStringLiteral("Set"),
        QStringLiteral("Change how the lister looks and behaves"),
        {arg("DUAL=", {QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("toggle"), QStringLiteral("vertical"), QStringLiteral("horizontal"), QStringLiteral("flip")},
             "One display or two, side by side or stacked"),
         arg("SOURCE=", {QStringLiteral("toggle")}, "Make the other display the source"),
         arg("DESTLOCK=", onOff(), "Keep the destination where it is"),
         arg("NAVLOCK=", onOff(), "The other display follows into and out of folders"),
         arg("VIEW=", {QStringLiteral("details"), QStringLiteral("list"), QStringLiteral("icons"), QStringLiteral("thumbnails")}, "View mode"),
         arg("HIDDEN=", onOff(), "Show hidden files"),
         arg("VIEWER=", onOff(), "The viewer pane (F3)"),
         arg("VIEWERPOS=", {QStringLiteral("right"), QStringLiteral("bottom"), QStringLiteral("left")}, "Where the viewer pane goes"),
         arg("THEME=", {QStringLiteral("next")}, "Theme by name, or the next one"),
         arg("SIZE=", {QStringLiteral("normal"), QStringLiteral("large"), QStringLiteral("larger"), QStringLiteral("bigger"), QStringLiteral("smaller")},
             "Text and icon size, for small screens"),
         arg("GROUPBY=", {QStringLiteral("none"), QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("modified"), QStringLiteral("extension"), QStringLiteral("folder"), QStringLiteral("artist"), QStringLiteral("album"), QStringLiteral("genre"), QStringLiteral("year"), QStringLiteral("camera"), QStringLiteral("taken")},
             "Group the list"),
         arg("GROUPCOLLAPSE=", {QStringLiteral("all"), QStringLiteral("none"), QStringLiteral("toggle")}, "Fold or open groups"),
         arg("FILTERBAR=", onOff(), "Show the filter bar"),
         arg("FILTER=", {}, "Show only matching names (a leading ! hides them instead; @name uses a saved filter)"),
         arg("FILTERSAVE=", {QStringLiteral("ask")}, "Save the filter in use under this name"),
         arg("STATUSDEF=", {QStringLiteral("default"), QStringLiteral("@Classic"), QStringLiteral("@Minimal"), QStringLiteral("@Focused item"),
                            QStringLiteral("@Drive details")},
             "This lister's own status bar (kept with its layout): a definition, @sample, or default"),
         arg("CHECKBOXES=", onOff(), "A check box on each row"),
         arg("FOLDERSIZES=", onOff(), "Calculate every folder's size"),
         arg("EXPAND=", {QStringLiteral("toggle"), QStringLiteral("on"), QStringLiteral("off"), QStringLiteral("none")}, "Open the selected folders in place, under themselves (Details view)"),
         arg("ZOOM=", {QStringLiteral("in"), QStringLiteral("out"), QStringLiteral("reset"), QStringLiteral("125"), QStringLiteral("150")}, "This lister's zoom (Shift+wheel)"),
         arg("FLATVIEW=", {QStringLiteral("off"), QStringLiteral("grouped"), QStringLiteral("ungrouped"), QStringLiteral("nofolders"), QStringLiteral("toggle")},
             "Everything under the folder in one list"),
         arg("TREE=", {QStringLiteral("off"), QStringLiteral("shared"), QStringLiteral("separate"), QStringLiteral("toggle")}, "Folder tree"),
         arg("FORMAT=", {QStringLiteral("save"), QStringLiteral("savesub"), QStringLiteral("clear"), QStringLiteral("savedefault"), QStringLiteral("cleardefault")},
             "Save or clear this folder's format"),
         arg("AUTOFORMAT=", onOff(), "Icons for folders of pictures"),
         arg("COLUMNSTOGGLE=", {QStringLiteral("created"), QStringLiteral("accessed"), QStringLiteral("extension"), QStringLiteral("permissions"), QStringLiteral("owner"), QStringLiteral("group"), QStringLiteral("title"), QStringLiteral("artist"), QStringLiteral("album"), QStringLiteral("track"), QStringLiteral("year"), QStringLiteral("genre"), QStringLiteral("length"), QStringLiteral("bitrate"), QStringLiteral("dimensions"), QStringLiteral("datetaken"), QStringLiteral("camera")},
             "Show or hide a column")},
        QStringLiteral("Set DUAL=vertical VIEW=details")};
    c << CommandInfo{QStringLiteral("Settings"), QStringLiteral("Preferences, and backing up or restoring every setting"),
                     {arg("BACKUP=", {QStringLiteral("ask")}, "Save every setting to one zip (ask: choose where)"),
                      arg("RESTORE=", {QStringLiteral("ask")}, "Put back settings from a backup zip"),
                      arg("QUIET", {}, "Restore without asking first"),
                      arg("PAGE=", {QStringLiteral("appearance"), QStringLiteral("theme"), QStringLiteral("labels"), QStringLiteral("aliases"),
                                    QStringLiteral("formats"), QStringLiteral("toolbars"), QStringLiteral("filetypes"), QStringLiteral("statusbar"), QStringLiteral("scripts"), QStringLiteral("viewer"), QStringLiteral("keys"), QStringLiteral("filters"), QStringLiteral("tabgroups"), QStringLiteral("contextmenu")},
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
                      arg("PLACE=", {QStringLiteral("top"), QStringLiteral("bottom"), QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("between"), QStringLiteral("menubar")},
                          "Where it goes (menubar: at the right end of the menu bar)"),
                      arg("NEWROW", {}, "Start a new row instead of sharing one"),
                      arg("SAMEROW", {}, "Between the displays: run on after the toolbar before it, in its row or column"),
                      arg("STATE=", onOff(), "Show or hide it"),
                      arg("CUSTOMIZE", {}, "Enter customize mode"),
                      arg("ARRANGE=", {QStringLiteral("classic")}, "The classic arrangement: Operations, the drives and Favorites between the displays, Views in the menu bar")},
                     QStringLiteral("Toolbar NAME=drives PLACE=between")};
    c << CommandInfo{QStringLiteral("Undo"), QStringLiteral("Undo a copy, move, new folder, trash or rename"),
                     {arg("ID=", {}, "Which one (the History window shows them); the latest when left out"),
                      arg("LIST", {}, "Show everything that can be undone"),
                      arg("LOG", {}, "Show the file log"),
                      arg("FIND=", {}, "With LOG: show only entries with this in their path")},
                     QStringLiteral("Undo LOG FIND=Galaga")};
    // Listed (and offered for completion) in alphabetical order.
    std::sort(c.begin(), c.end(), [](const CommandInfo &a, const CommandInfo &b) { return a.name.compare(b.name, Qt::CaseInsensitive) < 0; });
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
        b("File operations", "Undo", "edit-undo", "Undo the last copy, move, new folder, trash or rename (right: the list)", "Undo", "Undo LIST"),
        b("File operations", "Undo Rename", "edit-undo", "Undo the last rename", "Rename UNDO"),
        b("File operations", "File Log", "view-history", "Every file operation on record", "Undo LOG"),
        b("File operations", "New Folder", "folder-new", "Create folders", "CreateFolder"),
        b("File operations", "Copy As", "edit-copy", "Copy with a new name", "Copy AS"),
        b("File operations", "Copy If Newer", "edit-copy", "Copy, replacing only older files", "Copy WHENEXISTS=newer"),
        b("Selection", "Select...", "edit-select", "Select by name, kind, size, age or against the other display", "Select DIALOG"),
        b("Selection", "Select New", "edit-select", "Newer here or not in the other display (right: select what is the same in both)",
          "Select COMPARE=newerormissing", "Select COMPARE=same BOTH"),
        b("Metadata", "Metadata Pane", "document-properties", "Show and edit tags of the selection (Alt+Enter)", "Metadata PANE=toggle"),
        b("Metadata", "Copy Tags", "edit-copy", "Remember the tags of the file in focus", "Metadata COPY"),
        b("Metadata", "Paste Tags", "edit-paste", "Write the remembered tags to the selection (right: everything)", "Metadata PASTE", "Metadata PASTE ALL"),
        b("Metadata", "Number Tracks", "format-list-ordered", "Track numbers 1, 2, 3... in list order", "Metadata TRACK=auto"),
        b("Clipboard", "Cut", "edit-cut", "Cut the selection", "Clipboard CUT"),
        b("Clipboard", "Copy to Clipboard", "edit-copy", "Copy the selection to the clipboard", "Clipboard COPY"),
        b("Clipboard", "Paste", "edit-paste", "Paste into the source", "Clipboard PASTE"),
        b("Clipboard", "Paste as File", "edit-paste", "Paste a picture or text as a file you name", "Clipboard PASTE AS"),
        b("File operations", "Add to Archive", "archive-insert", "Put the selection in a zip, 7z or tar archive", "Archive CREATE",
          "Archive CREATE QUICK FORMAT=zip"),
        b("File operations", "Extract", "archive-extract", "Extract the selected archives here", "Archive EXTRACT",
          "Archive EXTRACT DEST"),
        b("File operations", "Clean Junk", "edit-clear", "Trash .DS_Store, Thumbs.db and other clutter in the source or the selected folders", "Clean"),
        b("File operations", "Split", "split", "Cut the selected file into pieces a FAT32 card can hold (right: asks the size)", "Split SIZE=fat32", "Split"),
        b("File operations", "Join", "merge", "Put pieces (name.001...) back together", "Join"),
        b("File operations", "Add to Steam", "steam", "Add the selected program to Steam as a non-Steam game", "Steam ADD"),
        b("File operations", "Checksums", "document-encrypt", "MD5 of the selection (right: SHA-256); verifies a selected .md5, .sha256 or .sfv list",
          "Checksum", "Checksum ALGO=sha256"),
        b("Navigation", "Network", "network-workgroup", "Windows and Samba machines on the network", "Go smb:/", "Server"),
        b("Navigation", "Terminal", "utilities-terminal", "Open Konsole in the source folder", "Terminal", "Terminal PATH={destpath}"),
        b("Labels and tags", "Tags", "tag", "Labels, ratings, tags and description of the selection", "Tag"),
        b("Labels and tags", "Rate Up", "rating", "One more star", "Tag RATING=+", "Tag RATING=-"),
        b("Labels and tags", "Red", "flag-red", "Red label (right-click: none)", "Tag LABEL=Red", "Tag LABEL=none"),
        b("Labels and tags", "Green", "flag-green", "Green label (right-click: none)", "Tag LABEL=Green", "Tag LABEL=none"),
        b("File operations", "Properties", "document-properties", "Size, dates and permissions of the selection", "Properties"),
        b("File operations", "Make Link", "insert-link", "Make links to the selection in the destination", "MakeLink DEST",
          "MakeLink"),
        b("File operations", "Touch", "chronometer", "Set the selection's modified date to now", "SetAttr MODIFIED=now"),
        b("File operations", "View", "view-preview", "Show the file in the viewer", "Show"),
        b("File operations", "Convert Images", "image-x-generic", "Convert, resize or turn the selected pictures", "Image", "Image CONVERT=png RUN"),
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
        b("View", "Thumbnails", "view-preview", "Pictures as thumbnails", "Set VIEW=thumbnails"),
        b("View", "Hidden", "view-hidden", "Show hidden files", "Set HIDDEN=toggle"),
        b("View", "Viewer", "view-preview", "Preview the file in focus (F3)", "Set VIEWER=toggle", "Set VIEWERPOS=bottom"),
        b("View", "Flat", "view-list-details", "Flat view", "Set FLATVIEW=toggle", "Set FLATVIEW=ungrouped"),
        b("View", "Group", "view-group", "Group by type", "Set GROUPBY=type", "Set GROUPBY=none"),
        b("View", "Filter", "view-filter", "Filter bar", "Set FILTERBAR=toggle"),
        b("View", "Check Boxes", "checkbox", "A check box per row", "Set CHECKBOXES=toggle"),
        b("View", "Sizes", "folder-stats", "Calculate every folder's size", "Set FOLDERSIZES=toggle"),
        b("View", "Expand", "go-down", "Open the selected folders in place (right: close them all)", "Set EXPAND=toggle", "Set EXPAND=none"),
        b("View", "Zoom", "zoom-in", "Bigger text in this lister (right: smaller)", "Set ZOOM=in", "Set ZOOM=out"),
        b("View", "Theme", "color-management", "Next theme", "Set THEME=next"),
        CatalogButton{QStringLiteral("Favorites"), QStringLiteral("Favorites"), QStringLiteral("bookmarks"),
                      QStringLiteral("Your favorite folders"), {}, {}, QStringLiteral("favorites")},
        CatalogButton{QStringLiteral("Favorites"), QStringLiteral("Frequent"), QStringLiteral("document-open-recent"),
                      QStringLiteral("The folders you use most"), {}, {}, QStringLiteral("frequent")},
        CatalogButton{QStringLiteral("Favorites"), QStringLiteral("Aliases"), QStringLiteral("folder-bookmark"),
                      QStringLiteral("Every alias"), {}, {}, QStringLiteral("aliases")},
        CatalogButton{QStringLiteral("Favorites"), QStringLiteral("Collections"), QStringLiteral("folder-favorites"),
                      QStringLiteral("Every collection"), {}, {}, QStringLiteral("collections")},
        CatalogButton{QStringLiteral("Favorites"), QStringLiteral("Servers"), QStringLiteral("network-server"),
                      QStringLiteral("The network and your saved servers"), {}, {}, QStringLiteral("servers")},
        b("Favorites", "Add Favorite", "bookmark-new", "Add the source folder to Favorites", "Favorite ADD", "Favorite ORGANIZE"),
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
        c("images", {}, "The selection has a picture in it"),
        c("archives", {}, "The selection has an archive in it"),
        c("dual", {}, "Two displays are showing"),
        c("dest", {}, "There is a destination folder"),
        c("destlocked", {}, "The destination is locked"),
        c("navlock", {}, "Navigation lock is on"),
        c("remote", {}, "The source is on a server or in an archive"),
        c("canup", {}, "The source has a parent folder"),
        c("favorite", {}, "The source folder is a favorite"),
        c("viewer", {}, "The viewer pane is showing"),
        c("back", {}, "There is history to go back to"),
        c("forward", {}, "There is history to go forward to"),
        c("clipboard", {}, "There are files on the clipboard"),
        c("hidden", {}, "Hidden files are showing"),
        c("tree", {}, "A folder tree is showing"),
        c("flat", {}, "Flat view is on"),
        c("flatgrouped", {}, "Flat view, grouped by folder"),
        c("flatungrouped", {}, "Flat view, ungrouped"),
        c("flatfiles", {}, "Flat view, ungrouped, files only"),
        c("grouped", {}, "The list is grouped"),
        c("filter", {}, "The filter bar is open"),
        c("checkboxes", {}, "Check boxes are on"),
        c("foldersizes", {}, "Every folder's size is being calculated"),
        c("view", {QStringLiteral("details"), QStringLiteral("list"), QStringLiteral("icons")}, "The view mode"),
        c("layout", {QStringLiteral("single"), QStringLiteral("vertical"), QStringLiteral("horizontal")}, "The display layout"),
        c("groupby", {QStringLiteral("none"), QStringLiteral("type"), QStringLiteral("name"), QStringLiteral("size"), QStringLiteral("modified"), QStringLiteral("extension"), QStringLiteral("folder"), QStringLiteral("artist"), QStringLiteral("album"), QStringLiteral("genre"), QStringLiteral("year"), QStringLiteral("camera"), QStringLiteral("taken")},
          "What the list is grouped by"),
        c("treemode", {QStringLiteral("off"), QStringLiteral("shared"), QStringLiteral("separate")}, "Which folder tree is showing"),
        c("theme", {}, "The theme, by name (spaces removed, for example dirduodark)"),
    };
}

} // namespace

namespace CommandCatalog {

const QList<CommandInfo> &commands()
{
    static const QList<CommandInfo> list = buildCommands();
    return list;
}

namespace {
QList<CommandInfo> &scriptList()
{
    static QList<CommandInfo> list;
    return list;
}
} // namespace

void setScriptCommands(const QList<CommandInfo> &list)
{
    scriptList() = list;
}

const QList<CommandInfo> &scriptCommands()
{
    return scriptList();
}

const CommandInfo *find(const QString &name)
{
    for (const CommandInfo &c : commands()) {
        if (c.name.compare(name, Qt::CaseInsensitive) == 0)
            return &c;
    }
    for (const CommandInfo &c : scriptList()) {
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
        for (const CommandInfo &c : commands() + scriptList()) {
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
