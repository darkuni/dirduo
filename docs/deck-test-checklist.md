# Deck test checklist

What the headless tests cannot cover: the real Flatpak sandbox, Plasma's theme
and Wayland session, the SD card and a real server. Roughly 15 minutes.

## Install

- [ ] Actions > newest green run > Artifacts > `linopus-x86_64.flatpak`. Unzip it.
- [ ] `flatpak install --user --reinstall linopus-x86_64.flatpak`
- [ ] Linopus is in the application menu and opens.

## Look

- [ ] Toolbar Theme button cycles Follow Desktop, Linopus Light, Linopus Dark, Workbench. Colors change everywhere (lists, menus, dialogs).
- [ ] In Linopus Dark the toolbar and file icons stay visible.
- [ ] Quit and reopen: the theme is the one you left.
- [ ] SOURCE and DEST each have a colored edge; Lock Dest changes the DEST color.

## Drives and trees

- [ ] Drives toolbar shows Root, Home and the SD card (by its label). Clicking it opens the card.
- [ ] Tree button: Home, Root, the SD card at the top. A folder on the card opens under the card's entry.
- [ ] Right-click Tree: one tree per display. Each follows its own display.
- [ ] Pull a USB stick or SD card while the tree is open: its entry goes away within a few seconds.

## Lists

- [ ] Group button (by type): headings with counts; the arrow folds a group; clicking a heading selects the group.
- [ ] Right-click a column header: add Created and Extension; Reset Columns puts them back.
- [ ] Drag column edges with the mouse and with a finger, the Name column too. Double-click the Name column's right edge: it fills the room again.
- [ ] Flat view > Ungrouped, select files from different subfolders, copy them to the other display: it asks Keep Folder Structure or Same Folder; try both.
- [ ] Right-click a folder > Add to Archive...: try 7z and tar.zst, Here and Destination. Add to "Name.zip" makes one straight away. Ctrl+Z removes it.
- [ ] Right-click a zip, a 7z and a RAR > Extract Here, Extract to Destination, Extract to "Name". Extract the same one twice: the second gets "(2)".
- [ ] Make a zip with a password, then Extract it: it asks; a wrong password says so and leaves nothing behind.
- [ ] The status line shows free space for the internal drive and the SD card. Copy something bigger than the card's free space: it stops at once with "not enough room".
- [ ] Copy files from an ungrouped flat view with Ctrl+C, paste with Ctrl+V: it asks Keep Folder Structure or Same Folder.
- [ ] Alt+Enter on a file, a folder and a few items: size (folders count up), dates, permissions. Tick Group > Write and change the date; OK; the list shows the new date.
- [ ] Right-click > Make Link in Destination on a ROM folder: the link opens in the other display. Ctrl+Z removes it.
- [ ] Right-click one or several pictures: Convert Image(s)... is there; it is not there when a non-picture is selected.
- [ ] Ctrl+F filter: type `*.png` and only PNGs stay; Esc clears it.
- [ ] Sizes button on a big folder (Emulation, for example): sizes and bars fill in without freezing.
- [ ] Flat button on a folder with subfolders: everything below, grouped by folder.
- [ ] A folder full of screenshots opens as thumbnails by itself; they fill in within a second or two. A folder of videos Dolphin has shown before has thumbnails too.

## Servers (needs the sandbox: SFTP and FTP are bundled in the Flatpak)

- [ ] Type `sftp://user@host/` in the address bar (your Unraid box works). It asks for the password, then lists the folder.
- [ ] Copy a file from the server to the SD card and back. A progress window shows for each.
- [ ] Open a `.zip` by double-clicking: it opens as a folder; copy a file out of it.

## File operations

- [ ] Copy a large file (a few GB) from internal storage to the SD card: progress, pause and resume work.
- [ ] Copy onto an existing file: the collision dialog shows both files; Rename keeps both under a new name.
- [ ] Delete goes to the trash on both internal storage and the SD card.

## Newer things (quick)

- [ ] View > Text and Icon Size > Larger: everything readable from arm's length; back to Normal.
- [ ] With Linopus open, start it again from the application menu: no second copy; the open one comes to the front. In Konsole: `flatpak run com.monroeworld.Linopus --tab ~/Downloads` opens a tab in it.
- [ ] In Steam, a game's Browse Local Files (or Firefox's Show in Folder) opens in Linopus if it is the default file manager (System Settings > Default Applications).
- [ ] In a big ROM folder, type a few letters of a game: it jumps there; Enter opens it. Type `>Set VIEW=icons` and Enter. Type `/sdcard` and Enter.
- [ ] ROMs on internal storage on one side, the SD card's ROMs on the other: Edit > Select by Comparing > Newer Here, or Not There selects what the card is missing.
- [ ] Copy something to the SD card, then Ctrl+Z: the copy goes (to the SD card's trash). Trash a file, Ctrl+Z: it is back. Tools > File Log shows both.
- [ ] In Details view, click the triangle beside a folder (or press Right): it opens in place under itself; Left closes it.
- [ ] Hold Shift and scroll (mouse wheel, or the right trackpad) over a list: that list zooms; the other display and a second lister stay as they are. Ctrl+0 resets. Try a two-finger pinch on the touchscreen too.
- [ ] Take a screenshot with Spectacle and copy it to the clipboard, then Ctrl+V in a folder: a PNG appears.
- [ ] Select a few files, Shift+F2: Advanced Rename shows the before and after; rename, then Edit > Undo Rename.
- [ ] Window > Customize Toolbars: drag a button to another spot, double-click one to edit, close.
- [ ] File > Settings > Back Up Settings: a zip lands where you choose.
- [ ] Tools > Synchronize between a folder on internal storage and one on the SD card: the list makes sense; Synchronize; Compare again says they match.
- [ ] Ctrl+Shift+F, name `*.d64` (or any type you have), Find: results open in a tab; copy one of them to the other display.
- [ ] Select a few files, Edit > Add to Collection > New Collection; Go > Collections opens it; Delete there leaves the real files alone.
- [ ] F3 opens the viewer pane; arrow through a folder of screenshots; View > Viewer Position > Bottom.
- [ ] Window > Layouts > Save This Layout; change things around; load it back.
- [ ] Ctrl+, opens Preferences; type "color" in the search box; change the SOURCE color in the Theme Editor (it shows at once); Cancel puts it back.
- [ ] Ctrl+D in a few folders, then the Favorites button: they are listed; Organize Favorites to group them.
- [ ] Tools > Find Duplicates on a ROM folder: groups show; check copies, then Move Checked to Trash.
- [ ] Select a few podcast MP3s, Shift+Alt+Enter: type an album name, Apply; Edit > Tags > Number Tracks. Ctrl+Z undoes it.
- [ ] A music folder: right-click the column header > Music > Artist and Album; they fill in. Group By > Artist. F3 on a song shows its tags.
- [ ] A folder of phone or camera photos: Pictures > Date Taken and Camera fill in; Group By > Date Taken.
- [ ] Select a few Steam screenshots, Tools > Convert Images: JPEG, Width 1280, Destination; Convert. The JPEGs land in the other display.

## Report back

For anything that fails: what you clicked, what happened, and a screenshot if it is visual.
