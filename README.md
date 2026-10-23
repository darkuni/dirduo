# Linopus

A source/destination file manager for Linux in the spirit of Windows Directory Opus. Two displays, one SOURCE and one DEST, and every command acts from one to the other.

Targets: SteamOS Desktop Mode on the Steam Deck (x86_64) and Steam Frame (aarch64), and any Linux desktop after that. Qt 6 Widgets, C++20, shipped as a Flatpak on the KDE runtime.

## Phase 1

- Listers: any number of windows, each single, dual vertical (side by side) or dual horizontal (top and bottom), switchable live from the toolbar (Vertical / Horizontal; right-click either for a single display), the View menu, or Ctrl+Shift+V / Ctrl+Shift+H / Ctrl+Shift+1
- SOURCE and DEST badges; Tab moves to the other display and makes it the source; the destination can be locked
- Navigation lock: the other display follows you into and out of folders (dev and staging trees side by side)
- Tabs per display with back/forward/up history; middle-click a folder to open it in a new tab
- Breadcrumb address bar per display: click a segment, drop down any segment's subfolders, click empty space (or Ctrl+L) to type a path with completion for folders and aliases
- Details, list and icon views; folders first; natural sort (audio2 before audio10)
- Inline rename: F2, name selected up to the extension; Up/Down/Tab commit and keep renaming the next item
- Aliases: `/downloads`, `/music`, `/sdcard`, `/steam` and the rest, usable anywhere a path is; add your own from Go > Aliases
- File operations run on their own threads, each in its own pop-up progress window (Opus style): current file and total progress, speed, time left, pause/resume, cancel, and Start Now for a job waiting behind another on the same drive. Jobs to the same drive queue; different drives run at once. Windows close themselves when a job finishes cleanly and stay open listing anything that failed. Closing a window mid-job keeps it running; Window > File Operations (Ctrl+J) brings it back.
- Collision dialog with both files side by side: Replace, Replace if Newer, Skip, Rename, Cancel, and apply-to-all
- Freedesktop trash (shared with Dolphin and the desktop), including per-device trash on SD cards and USB drives
- Clipboard cut/copy/paste of files, interoperable with Dolphin and Nautilus
- Drag and drop between displays and with other apps: same drive moves, another drive copies; Ctrl copies, Shift moves
- Three-button toolbar: left, right and middle click run different commands
- Drives toolbar: Root, Home, and every mounted SD card, USB stick or extra disk (anything under `/run/media`, `/media` or `/mnt`), updating as cards and sticks come and go. Click opens the drive in the source, middle-click in a new tab, right-click in the destination. The drive the source is on stays highlighted; hover for free space
- Toolbars go anywhere: top, bottom, left, right, or between the two displays (where Drives starts). Between the displays they run vertically when the displays are side by side and horizontally when stacked. Several toolbars stack as rows or columns. Right-click a toolbar's handle or empty space, or use Window > Toolbars, to move, stack, reorder or hide it. Placement is saved per lister
- Session: listers, tabs, layout and views come back next launch

Servers and archives (KIO):

- Type a server address in any address bar: `sftp://deck@nas/media`, `ftp://host/pub`, `smb://box/share`, `webdav://...`. Network, as in Dolphin: the Network button on the drives bar, the Network group in the folder tree and Go > Network and Servers open Windows Shares (`smb:/`: the Windows and Samba machines on the network and their shares, found by WS-Discovery and NetBIOS; the Flatpak bundles Samba's client). Server address book: Add Server... (name, kind, server, port, user, folder; or Add This Location while on a server) keeps servers by name in all of those places; Manage Servers... edits them. Passwords stay with KDE's wallet. Server folders behave like local ones: browse, tabs, Copy/Move between a server and a drive (either direction), Copy As, Delete, Rename, New Folder, drag and drop
- Copies to and from servers run in their own progress windows with pause, cancel and speed, and use the same "already exists" dialog as local copies. Jobs to the same server queue behind each other
- Archives open as folders: double-click (or Enter) on a zip, tar or 7z goes inside it; Up leaves it. Copy files straight out of them. Right-click > Open in Application still hands the archive to an archiver
- Aliases can point at servers: an alias `nas` = `sftp://deck@nas/media` makes `/nas/Movies` work anywhere
- Servers have no trash, so Delete on a server is permanent and says so before it happens
- Passwords go through KDE's password prompt and wallet; SFTP also uses your `~/.ssh` keys

## Phase 2

- Themes: Follow Desktop, Linopus Light, Linopus Dark and Workbench (Amiga blue, white and orange), from the Theme button, View > Theme or `Set THEME=`. File types have their own colors; SOURCE, DEST and a locked DEST each have an accent edge. Themes are `.ini` files: drop your own in `~/.config/linopus/themes`
- Labels: color, bold or italic by wildcard (`labels.ini`)
- Folder trees: one shared tree that follows the source, or one per display. The tree starts from Home, Root, each SD card, USB stick and disk, and your aliases
- Columns: drag any column's edge to size it, the name column too (it fills the leftover room until you drag it; double-click its edge to make it fill again). Right-click the column header for Created, Accessed, Extension, Permissions, Owner and Group, Group By, and folder sizes
- Grouping by type, name, size, date, extension or folder. Click a heading to select its group; click its arrow (or Left/Right) to fold it
- Filter bar (Ctrl+F): text or wildcards, show or hide matches, files, folders or both
- Check boxes on every row, so a stray click never drops a selection
- Folder sizes on hover; Calculate All Folder Sizes (Ctrl+Shift+Z) sizes every folder with a bar
- Flat view: everything under the folder, grouped by folder or ungrouped (one list, with or without the folders). The Flat button turns on the kind used last; press and hold it (or a long touch) for all the kinds. Copying or moving items that came from folders further down asks whether to keep that folder structure at the destination or put everything in the same folder (`Copy STRUCTURE=keep|flat` skips the question)
- Properties (Alt+Enter, or right-click > Properties...): location, type, size (folders are counted in the background), where a link points, owner, dates and permissions. Change the modified and accessed dates and the permissions for one item or many, optionally for everything inside folders
- Links: right-click > Make Link Here ("Name (link)") or Make Link in Destination. `MakeLink RELATIVE` makes links that keep working when both move together (an SD card in another machine). Undo removes them
- Location bar laid out as in Opus: Back, Forward, Up, Refresh, Home (a "best favorite": middle-click it, or right-click > Set to This Folder, to make the current folder Home), a Favorites star (pick one, add this folder, organize), the folder's icon (drag it to copy, move or link the folder), the path, a list of recent folders, and (with two displays) Open This Folder in the Other Display and Swap Folders. The bar takes the display's role color (source, destination, locked destination)
- Shell script buttons: a button whose function starts with `#!/bin/bash` (or any `#!`) runs as a script on the real system (outside the Flatpak). The selected items are `"$@"`, it starts in the source folder, and `$LINOPUS_SOURCE`, `$LINOPUS_DEST` and `$LINOPUS_FOCUSED` are set. `# linopus: terminal` runs it in Konsole, `# linopus: output` always shows what it printed (otherwise only when it fails), `# linopus: norefresh` leaves the listings alone. The button editor's New Script puts in a template
- Open With: right-click > Open With lists the applications for the file's type (from the system's .desktop files, Flatpak apps included), plus Other Application... to pick any app, search by name, and Always use it for that type. Linopus's choice wins over the desktop's when opening files. `OpenWith APP=Kate DEFAULT`
- Viewer pane: pictures, text (NFO/DIZ in code page 437), hex, what is inside any archive (zip, 7z, RAR, tar, ISO...), and music and video with Play, a position bar and the time; nothing plays until asked, and closing the pane or moving on stops it
- Picture viewer (right-click > View, or the `Show` command): one picture at a time in its own window, stepping through the folder (or the selected pictures) with the arrows, Home/End, the wheel to zoom (Ctrl+wheel to step), 0 to fit, 1 for 100%, R/L to turn, F or a double-click for full screen, Esc to close
- Help > About shows which build this is
- Right-click empty space > Open Konsole in This Folder (also Tools > Open Konsole Here, Shift+F4, and the `Terminal` command). Uses Konsole, or the first other terminal installed; from the Flatpak it opens on the real system
- Free space: the status line shows how much room is left on the drive or card (in red when it is nearly full), and a copy that will not fit stops before writing anything
- Convert Images is on the right-click menu when every selected file is a picture
- Archives (libarchive): right-click > Add to Archive... makes zip, 7z or tar (gz, xz, bz2, zstd) archives here or in the destination; Add to "Name.zip" makes one without asking. Extract Here, Extract Here and Trash Archive, Extract to Destination and Extract to "Name" unpack zip, 7z, RAR, tar, ISO, LHA/LZH, CAB and more. Extract Here puts an archive holding one folder straight in, and anything else in a folder named after the archive. Password-protected zip and 7z archives ask for the password (and Add to Archive can encrypt a zip with AES-256). Nothing is overwritten unless asked, names that are taken get new ones, entries that point outside the folder are left out, and both can be undone
- Folder formats: save columns, sort, grouping and view for a folder, a folder and its subfolders, or as the default. Folders of pictures open as icons by themselves

## Phase 3 (in progress)

- Customize mode (Window > Customize Toolbars, or right-click a toolbar > Customize): drag buttons within a toolbar or to another one (Ctrl+drag copies), double-click to edit, right-click to duplicate, delete, add a separator or a new button. The dialog makes new toolbars, shows and hides them, and lists ready-made buttons to drag onto any bar
- Button editor: label, icon, tooltip, commands for left, right and middle click, a hotkey, and conditions for when the button is grayed out or shown pressed. A button can be a menu of other buttons, nested as deep as you like
- Buttons follow the lister: Copy and Move gray out without a selection or destination, toggles like Tree and Hidden show pressed while on
- Drop files on a button to run it on those files instead of the selection
- Command console (Ctrl+Shift+P): type any command and run it in the lister; Tab completes, Up/Down for history, `help` and `help Set` explain

## Also in

- Advanced Rename (Shift+F2, or right-click Rename): wildcards, find and replace or regular expressions; numbering (`#`, `[##]`), dates (`[date]`, `[mdate]`), `[name]`, `[ext]`, `[parent]`; case changes (Title Case keeps Roman numerals: Turrican II); a live before and after preview that refuses clashes; presets; Edit > Undo Rename. Scriptable: `Rename OLDNAME=*.jpg NEWNAME="Holiday ###.*"`
- Paste as file: with a picture or text on the clipboard, Paste makes a PNG or TXT in the source (Edit > Paste as File... to name it)
- Print Folder (File > Print Folder): the listing as shown, as text, CSV or HTML, to the clipboard or a file
- Settings backup (File > Settings): every setting in one zip, restored in one step on another machine
- Text and icon size (View > Text and Icon Size): Large and Larger for the Deck's screen
- Synchronize (Tools > Synchronize): compare the source and destination (or any two folders) by date, size or contents; see every difference with what will happen to it, change any of them, then copy. Update the right, mirror to it (extras go to the trash), or both ways. Allows for the 2-second timestamps of FAT SD cards. `Sync ... RUN` does it from a button without asking
- Find (Tools > Find, Ctrl+Shift+F): by name (text, wildcards or a regular expression), text inside files, size, modified date, files or folders, under any folders. Results open in a tab as the Find Results collection, where every command works on them; searches can be saved and run again. `Find NAME=*.d64 NEWER=30 RUN` from a button
- Collections (Go > Collections, `/collections`): virtual folders of links to files anywhere. Edit > Add to Collection, or copy into a collection shown in the other display; Delete there only takes the link out; renaming the real file keeps the link. Each is a text file in `~/.config/linopus/collections`
- Viewer pane (F3, View > Viewer Pane): shows the file in focus beside or below the displays. Pictures fit the pane (click for actual size, photos the right way up), text and code as text, `.nfo` and `.diz` in the DOS code page with their box drawing, other files as hex (the Hex button shows any file's bytes), zips as a list of what is inside, folders as a count. Arrowing through a folder only loads where you stop
- Layouts (Window > Layouts): save a lister's whole arrangement (single or dual, trees, toolbars and where they sit, viewer pane, views) under a name and switch to it in one click; with or without its folders. `Layout LOAD="ROM sorting"` from a button
- Preferences (File > Preferences, Ctrl+,): a search box over every setting; Appearance (theme, size); a Theme Editor for every color, the SOURCE/DEST accents and each file type, live, saved as your own theme (a built-in is never overwritten: edits become "Name (mine)"); Labels; Aliases; Folder Formats; Toolbars. Cancel undoes what you previewed
- Favorites (Favorites menu, the Favorites toolbar button, Ctrl+D to add): your folders in your order, in groups if you like; Organize Favorites renames, reorders and groups them. Frequent Folders lists the folders you use most, older visits counting for less
- Self-filling toolbar menus: a button can be a menu of your Favorites, Frequent folders, Aliases or Collections that stays up to date (customize mode, Commands > Favorites)
- Find Duplicates (Tools > Find Duplicates): identical files by contents (renamed copies too), by name and size, or by name, across several folders; check all but the newest, oldest or shortest path in each group and send the rest to the trash
- Convert Images (Tools > Convert Images): PNG, JPEG, WebP, BMP and whatever else the image plugins write; resize by percentage, to fit, by width or height, or exactly; turn and flip; save next to each picture or in another folder. A row per picture shows its new name and size before anything is written. The format, quality and size are remembered
- Starting from outside: `linopus <folders, files, aliases, file:// links or server addresses>` opens each in a new lister; a file opens its folder with the file selected ("Show in folder"); `--tab` opens them as tabs in the active lister; `--cmd "Go /sdcard"` runs commands there (DOpusRT-style). When Linopus is already running, a new launch hands everything to it and quits; `--new-instance` starts a separate copy
- Find-as-you-type: start typing in a list and a field opens under it. Plain text jumps to the first name that starts with it (or contains it), Up/Down for the next match, Enter opens it; wildcards (`*.d64`) select every match; `>` runs a command (Tab completes); `/alias` or `~/path` goes there; Ctrl+F turns the text into the filter bar; Esc closes
- Select by comparing (Edit > Select by Comparing, Edit > Select... Ctrl+Shift+S): what is newer here or not in the other display (what an update would copy), only here, newer, older, the same in both (optionally selected on both sides) or a different size; combine with name, kind, size and age, and add to or take out of the selection. Dates within 2 seconds count as the same, for FAT-formatted SD cards
- Undo (Ctrl+Z, Edit > Undo List, `Undo`): copies, moves, new folders and pasted files, sending to the trash, and renames. Copies come out by going to the trash; a copy that replaced a file stays (the old file cannot come back); things moved come back where they were, folders made again if needed; trashed items come back out of the trash. Undoing is itself on the list, so it can be redone. Copying into a folder that was already there undoes only what the copy added
- File log (Tools > File Log, `Undo LOG FIND=name`): every copy, move, trash, delete, rename and undo with its time and where things went, the last 5000 kept in ~/.local/share/linopus/file-log.tsv; filter by name or path to trace a file that went missing, copy or save it
- Expandable folders (Details view): click a folder's triangle, press Right, or `Set EXPAND=toggle` and its contents open under it, indented, as deep as you like; Left closes it or jumps to the folder an item is in. Opened folders keep up with changes on disk, stay in place through sorting and grouping, and work on servers too
- Zoom per file display, as in Opus: Shift+scroll wheel over a list (or a pinch on the Deck's screen or a touchpad) makes that list's text, icons, thumbnails and columns bigger or smaller, from 50% to 300%, on top of the app-wide text size; Ctrl+= / Ctrl+- / Ctrl+0 zoom the source. Each display keeps its own zoom, also in saved layouts; a shared folder tree follows the source
- Thumbnails view (View > Thumbnails, Ctrl+4; folders that are mostly pictures open in it): pictures as themselves, read in the background. Shares the standard ~/.cache/thumbnails with Dolphin and Gwenview, so videos and documents they have already done show too, and what Linopus makes they reuse. Icons view shows thumbnails at its smaller size
- Editing tags: the Metadata pane (View > Metadata Pane, Shift+Alt+Enter) shows the selection's tags; with several files, shared values show and differing ones say "(several values)", and a field you type in is written to all of them on Apply (untouched fields stay). Music: title, artist, album artist, album, track, disc, year, genre, comment (MP3, FLAC, Ogg, Opus, M4A, WAV, AIFF, WMA...). Photos: date taken, description, artist, copyright (JPEG, PNG, WebP, TIFF). Edit > Tags: Copy Tags and Paste Tags (artist, album, genre, year, comment; Paste All adds titles, tracks and dates) for podcast episodes, Number Tracks in list order, and moving photo dates an hour either way for a camera clock left on the wrong time. All of it can be undone
- Music and photo columns (right-click a column header > Music or Pictures): Title, Artist, Album, Track, Year, Genre, Length and Bitrate from music tags (TagLib); Dimensions, Date Taken and Camera from photos (Exiv2). C64 SID tunes show their name, composer and release line. Read in the background as you browse, then sortable and groupable (Group By > Artist, Album, Genre, Year, Camera, Date Taken). The viewer pane shows the full tags, and the camera line under a photo. Column layouts saved before these columns existed keep them hidden

## Commands

Every button, menu item, hotkey, drop and inline rename runs one of these. Button functions can hold several lines; they run in order and stop at the first failure.

| Command | Arguments |
| --- | --- |
| `Go` | `<path or /alias>`, `UP`, `BACK`, `FORWARD`, `REFRESH`, `SWAP`, `NEWTAB`, `OPENINDEST` |
| `Copy` | `MOVE`, `AS`, `FILE=...`, `TO=...`, `WHENEXISTS=ask\|replace\|newer\|skip\|rename` |
| `Move` | same as `Copy MOVE` |
| `Delete` | `NORECYCLE` (permanent, asks first), `QUIET`, `FILE=...` |
| `Rename` | no arguments: rename in place; `FROM=... TO=...`; `ADVANCED`; `UNDO`; `OLDNAME=... NEWNAME=...` with `MODE=wildcard\|findreplace\|regexp`, `CASE=lower\|upper\|title\|sentence`, `EXTCASE=`, `IGNOREEXT`, `CASESENSITIVE`, `START=`, `STEP=`, `WIDTH=`, `TRIM`, `PRESET=` |
| `CreateFolder` | `NAME="A, B, C"` makes all three; no name asks; `IN=...` |
| `Archive` | `CREATE` (asks), `NAME=` (its extension picks the format), `QUICK`, `FORMAT=zip\|7z\|tar\|tar.gz\|tar.xz\|tar.bz2\|tar.zst`; `EXTRACT`, `MODE=smart\|folder\|here`; `DEST`, `TO=<folder>`, `WHENEXISTS=rename\|replace\|skip`, `FILE=...` |
| `Clipboard` | `COPY`, `CUT`, `PASTE` (a picture or text pastes as a new file), `AS` |
| `Print` | `FOLDER`, `AS=text\|csv\|html`, `TO=clipboard\|ask\|<file>`, `SELECTED` |
| `Layout` | `SAVE=<name>\|ask` (`FOLDERS` to keep folders and tabs), `LOAD=<name>`, `DELETE=<name>` |
| `Favorite` | `ADD`, `REMOVE`, `PATH=`, `NAME=`, `GROUP=`, `GO=<name>`, `ORGANIZE` |
| `Find` | `NAME=`, `REGEX`, `CASE`, `CONTAINS=`, `MINSIZE=10M`, `MAXSIZE=`, `NEWER=<days or date>`, `OLDER=`, `TYPE=files\|folders\|both`, `IN=...`, `HIDDEN`, `NORECURSE`, `COLLECTION=`, `PRESET=`, `RUN` |
| `Collection` | `ADD=<name>\|ask`, `REMOVE`, `CREATE=`, `DELETE=`, `OPEN=<name>`, `NEWTAB` |
| `Metadata` | `TITLE=`, `ARTIST=`, `ALBUMARTIST=`, `ALBUM=`, `GENRE=`, `COMMENT=`, `YEAR=`, `TRACK=<n>\|auto` (`START=`), `DISC=`, `TAKEN=`, `SHIFT=+1h\|-30m\|+2d`, `DESCRIPTION=`, `COPYRIGHT=` (empty clears), `COPY`, `PASTE` (`ALL`), `FILE=`, `PANE=toggle\|on\|off` |
| `Undo` | no arguments: the latest; `ID=`, `LIST`, `LOG` (`FIND=`) |
| `OpenWith` | `APP=<name or desktop id>` (asks when left out), `DEFAULT`, `FILE=...` |
| `Server` | `GO=<name>` (`NEWTAB`, `OPENINDEST`), `ADD` (asks) or `ADD URL=... NAME=...`, `REMOVE=<name>`; nothing: the list |
| `Show` | `FILE=...` (the picture viewer) |
| `Sync` | `LEFT=`, `RIGHT=`, `MODE=update\|mirror\|twoway`, `COMPARE=datesize\|date\|size\|contents`, `EXCLUDE=*.tmp;...`, `RUN` |
| `Terminal` | `PATH=<folder>` (else the source folder), `PROGRAM=konsole\|gnome-terminal\|...` |
| `Duplicates` | `IN=...` (several allowed), `BY=contents\|namesize\|name`, `MINSIZE=<bytes>`, `HIDDEN` |
| `MakeLink` | `DEST`, `TO=<folder>`, `NAME=`, `RELATIVE`, `FILE=...` |
| `Properties` | `FILE=...` |
| `SetAttr` | `MODIFIED=<date>\|now`, `ACCESSED=`, `PERMS=644\|rwxr-xr-x\|u+x,go-w`, `RECURSE`, `FILE=...` (nothing to change: the Properties window) |
| `Image` | `CONVERT=png\|jpg\|webp\|...\|keep`, `QUALITY=1-100`, `WIDTH=`, `HEIGHT=` (both: fit inside), `PERCENT=`, `STRETCH`, `ENLARGE`, `ROTATE=90\|180\|270`, `FLIP=h\|v\|both`, `TO=<folder>\|dest`, `WHENEXISTS=rename\|replace\|skip`, `FILE=...`, `RUN` (no window) |
| `Settings` | no arguments: Preferences; `PAGE=appearance\|theme\|labels\|aliases\|formats\|toolbars`; `BACKUP=<file>\|ask`, `RESTORE=<file>\|ask`, `QUIET` |
| `Set` | `DUAL=on\|off\|toggle\|vertical\|horizontal\|flip`, `SOURCE=toggle`, `DESTLOCK=`, `NAVLOCK=`, `VIEW=details\|list\|icons\|thumbnails`, `HIDDEN=`, `VIEWER=`, `VIEWERPOS=right\|bottom\|left`, `THEME=<name>\|next`, `SIZE=normal\|large\|larger\|bigger\|smaller`, `ZOOM=in\|out\|reset\|<50-300>`, `EXPAND=toggle\|on\|off\|none`, `GROUPBY=none\|type\|name\|size\|modified\|extension\|folder\|artist\|album\|genre\|year\|camera\|taken`, `GROUPCOLLAPSE=all\|none\|toggle`, `FILTERBAR=`, `FILTER=<text>` (a leading `!` hides matches), `CHECKBOXES=`, `FOLDERSIZES=`, `FLATVIEW=off\|grouped\|ungrouped\|nofolders\|toggle`, `TREE=off\|shared\|separate\|toggle`, `FORMAT=save\|savesub\|clear\|savedefault\|cleardefault`, `AUTOFORMAT=`, `COLUMNSTOGGLE=<column>`. Switches take `on\|off\|toggle` |
| `Select` | `ALL`, `NONE`, `INVERT`, `PATTERN=*.mp3`, `COMPARE=newerormissing\|missing\|newer\|older\|same\|different` (`BOTH`), `KIND=`, `TYPE=`, `MINSIZE=`, `MAXSIZE=`, `NEWER=`, `OLDER=`, `ADD`, `DESELECT`, `DIALOG` |
| `Toolbar` | `NAME=buttons\|drives\|<yours>`, `PLACE=top\|bottom\|left\|right\|between`, `NEWROW`, `STATE=on\|off\|toggle`, `CUSTOMIZE` |

Argument codes: `{sourcepath}`, `{destpath}`, `{filepath}` (every selected item), `{file}` (names only), `{focusedpath}`, `{dlgstring|Prompt text}`. Add `$` to require a value: `{filepath$}` stops the function when nothing is selected. Codes expand to whole arguments, so paths with spaces need no quoting.

Example button (left-click):

```
Go /downloads
Select PATTERN=*.mp3
Copy TO=/music/incoming WHENEXISTS=newer
```

## Aliases

A path starting with `/name` resolves through the alias table when `name` is an alias. Real top-level folders always win: `/usr` is `/usr`. Alias names that would shadow something in `/` are refused.

Built-in: `desktop`, `documents`, `downloads`, `music`, `pictures`, `videos`, `templates`, `config`, `linopus`, `trash`, `steam`, `temp`, `sdcard` (the mounted microSD card), `removable` (`/run/media/<user>`).

Your own aliases live in `~/.config/linopus/aliases.ini`:

```ini
[Aliases]
websites=/home/deck/sites
pods=/downloads/podcasts
```

## Keys

| Key | Action |
| --- | --- |
| Tab | Switch to the other display |
| Enter / double-click | Open |
| Backspace, Alt+Up | Up one folder |
| Alt+Left / Alt+Right | Back / Forward |
| Ctrl+L, Alt+D | Type a location |
| F2 | Rename in place |
| Shift+F2 | Advanced Rename |
| Del / Shift+Del | Trash / delete permanently |
| Ctrl+X / C / V | Cut / copy / paste files |
| Ctrl+Shift+N | New folder(s) |
| Ctrl+T / Ctrl+W | New tab / close tab |
| Ctrl+Tab | Next tab |
| Ctrl+1 / 2 / 3 | Details / list / icons |
| Ctrl+H | Hidden files |
| Alt+Enter | Properties |
| Shift+Alt+Enter | Metadata pane |
| Shift+wheel, Ctrl+= / Ctrl+- / Ctrl+0 | Zoom the list in, out, back to 100% |
| Ctrl+1 / 2 / 3 / 4 | Details, List, Icons, Thumbnails |
| Ctrl+A, Ctrl+Shift+A, Ctrl+I | Select all, none, invert |
| Ctrl+Shift+S | Select by pattern |
| F5 | Refresh |
| Ctrl+N | New lister |
| Ctrl+Shift+V / H / 1 | Dual vertical / dual horizontal / single |
| Ctrl+J | Show file operation windows |
| Ctrl+F | Filter bar |
| Ctrl+Shift+Z | Calculate all folder sizes |
| Ctrl+Shift+P | Command console |
| Ctrl+Shift+F | Find |
| Ctrl+D | Add the source folder to Favorites |
| Ctrl+, | Preferences |
| F3 | Viewer pane |

## Toolbar files

Each toolbar is `~/.config/linopus/toolbars/<id>.json`. Customize mode writes them; they are plain JSON to copy or hand-edit. The standard toolbar has no file until you change it, so new standard buttons keep arriving until then.

```json
{
  "name": "Emulation",
  "buttons": [
    { "label": "Copy", "icon": "edit-copy", "left": "Copy", "right": "Copy AS",
      "enableWhen": "selection && dest" },
    { "separator": true },
    { "label": "ROMs", "icon": "input-gaming", "left": "Go /roms", "middle": "Go /roms NEWTAB",
      "key": "Ctrl+Alt+R" },
    { "label": "Systems", "icon": "folder-games", "menu": [
        { "label": "C64", "left": "Go /roms/c64" },
        { "label": "Amiga", "left": "Go /roms/amiga" } ] }
  ]
}
```

A button with `"dynamic": "favorites"` (or `frequent`, `aliases`, `collections`) is a menu that fills itself.

`enableWhen` grays a button out when false; `checkWhen` shows it pressed when true. They combine these words with `&&`, `||`, `!` and parentheses: `selection`, `files`, `folders`, `single`, `multiple`, `dual`, `dest`, `destlocked`, `navlock`, `remote`, `canup`, `favorite`, `back`, `forward`, `clipboard`, `hidden`, `tree`, `flat`, `grouped`, `filter`, `checkboxes`, `foldersizes`, and `view=`, `layout=`, `groupby=`, `treemode=`, `theme=`.

## Files

- `~/.config/linopus/linopus.ini`: session and window layout
- `~/.config/linopus/aliases.ini`: your aliases
- `~/.config/linopus/toolbars/`: button bars
- `~/.config/linopus/themes/`: your themes
- `~/.config/linopus/labels.ini`: wildcard labels
- `~/.config/linopus/formats.ini`: folder formats
- `~/.config/linopus/rename.ini`: Advanced Rename presets
- `~/.config/linopus/find.ini`: saved searches
- `~/.config/linopus/collections/`: collections, one path per line
- `~/.config/linopus/favorites.json`, `frequent.ini`: favorites and folder visit counts
- `~/.config/linopus/layouts/`: saved layouts

Inside the Flatpak these are still the real `~/.config/linopus`, so the configuration copies straight to another machine.

## Building

### On the Steam Deck (Distrobox)

SteamOS's system partition is read-only, so development happens in a container. Distrobox ships with SteamOS.

```sh
distrobox create --name linopus-dev --image archlinux:latest
distrobox enter linopus-dev
sudo pacman -Syu --needed base-devel cmake ninja git qt6-base qt6-svg breeze-icons
git clone <repo url> && cd linopus
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/linopus
```

### Flatpak on the Deck

Two ways to get a test build.

**Build it on the Deck** (no GitHub needed). Copy this folder to the Deck, then in Desktop Mode open Konsole in it and run:

```sh
sh scripts/build-flatpak-on-deck.sh
```

It installs Flathub's own Flatpak builder and the KDE 6.10 SDK as user Flatpaks (nothing touches the read-only system), builds Linopus, and installs it. The first run downloads about 1 GB. Linopus then appears in the application menu; it also runs as `flatpak run com.monroeworld.Linopus`.

**From CI.** Every push to GitHub builds `linopus-x86_64.flatpak` and `linopus-aarch64.flatpak` (Actions > the run > Artifacts). Download the x86_64 one on the Deck, unzip it, then:

```sh
flatpak install --user linopus-x86_64.flatpak
```

### Tests and screenshots

```sh
ctest --test-dir build --output-on-failure
tests/remote/run-remote-tests.sh build   # starts an FTP and SSH server on localhost, needs KIO
QT_QPA_PLATFORM=offscreen ./build/linopus_snapshot /tmp/linopus-shots
```

`linopus_snapshot` builds a sample folder tree, drives the lister through a few commands and saves a PNG at each step.

`linopus_sizzle out-dir` makes the showcase screenshots: a made-up Deck (at `/home/deck` and `/run/media/deck`, removed afterwards, so run it in a throwaway container) shown in every theme and the main layouts, plus two contact sheets.

## Known gaps

- **Local copies run on Linopus's own engine, remote ones on KIO.** Local copies have per-file progress and the folder-merge rules described above; remote copies follow KIO's rules (a folder copied onto an existing folder merges into it).

- **Retry / Skip / Abort** is asked for local copies and moves and when a server connection drops during a copy (Retry copies again, skipping what already arrived in full); other server errors follow KIO's own rules.
- **A single display uses the most recently used other lister as its destination**, as in Opus; with only one lister open, Copy asks for a folder.
