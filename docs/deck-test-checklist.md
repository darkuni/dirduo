# Deck test checklist

What the headless tests cannot cover: the real Flatpak sandbox, Plasma's theme
and Wayland session, the SD card and a real server. Roughly 15 minutes.

## Install

- [ ] Actions > newest green run > Artifacts > `dirduo-x86_64.flatpak`. Unzip it.
- [ ] `flatpak install --user --reinstall dirduo-x86_64.flatpak`
- [ ] Directory Duo is in the application menu and opens.
- [ ] Coming from Linopus: `flatpak uninstall --user com.monroeworld.Linopus`. Directory Duo opens with your old toolbars, favorites, theme and labels.

## Look

- [ ] Toolbar Theme button cycles Follow Desktop, DirDuo Light, DirDuo Dark, Workbench. Colors change everywhere (lists, menus, dialogs).
- [ ] In DirDuo Dark the toolbar and file icons stay visible.
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
- [ ] Right-click an archive > Extract Here and Trash Archive: the contents appear and the archive is in the trash. Ctrl+Z brings the archive back.
- [ ] Press and hold the Flat button (and a long touch): Off, Grouped by Folder, Ungrouped, Ungrouped, Files Only.
- [ ] Location bar: Refresh, Home, the star's favorites, drag the folder icon to the other display, the recent-folders arrow, then the two buttons at the far right: this folder into the other display, and swap the two. Source and destination bars have different colors.
- [ ] Right-click empty space in a list > Open Konsole in This Folder: Konsole opens there. Shift+F4 does the same.
- [ ] Middle-click the Home button in a folder; go elsewhere; click Home: back in that folder (in either display). Right-click Home > Reset.
- [ ] Network button on the drives bar: your Windows/Samba machines show up. Open a share, sign in, copy both ways.
- [ ] Network arrow > Add Server...: add your NAS (SFTP or SMB). It appears in the arrow menu, Go > Network and Servers, and the tree's Network group. Manage Servers... edits and removes.
- [ ] Customize a toolbar, add a button, New Script; select some files and click it: the output window lists them. Try `# dirduo: terminal`, and a script that fails (`exit 1`).
- [ ] Right-click a ROM or a text file > Open With: your apps (Flatpak ones too) are listed with icons. Other Application...: tick Always use; double-clicking the file now opens it there.
- [ ] F3 viewer: an MP3 shows its tags and Play; it plays, the bar moves. Arrow to another file: it stops. A 7z or RAR lists what is inside.
- [ ] Right-click a screenshot > View: arrows step through the folder, F for full screen, the wheel zooms, Esc closes.
- [ ] Preferences > General: turn off free space and counts; the status line follows. Pick another terminal. File Types lists your Always-use choices.
- [ ] Help > About shows the build (commit and date).
- [ ] Copy from a card and pull it out halfway (or copy an unreadable file): it asks Retry / Skip / Skip All / Abort. Put the card back and Retry.
- [ ] Copy a big folder from the NAS and unplug the network for a moment: it asks Retry; plug back in and Retry, it carries on without copying finished files again.
- [ ] Navigation Lock with a server on one side and a local folder on the other: going into and out of folders follows on both.
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
- [ ] With DirDuo open, start it again from the application menu: no second copy; the open one comes to the front. In Konsole: `flatpak run com.monroeworld.DirDuo --tab ~/Downloads` opens a tab in it.
- [ ] In Steam, a game's Browse Local Files (or Firefox's Show in Folder) opens in DirDuo if it is the default file manager (System Settings > Default Applications).
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
- [ ] Right-click a few files > Label > Green: their names turn green. Alt+4 gives them four stars. Right-click a column header > Labels and Tags > Rating and Label. Group By > Label.
- [ ] Ctrl+Shift+T on two files: add the tag `demo`, a description, OK. Open the same folder in Dolphin: the tags and stars are there too. Copy one to the SD card: in DirDuo it keeps them.
- [ ] Ctrl+Shift+P, `Select TAG=demo`: both are selected. Ctrl+Z undoes the last label change.
- [ ] Right-click a big file > Checksums > SHA-256...: the window works it out with a progress bar; Save List. Select the .sha256 file it made and right-click > Verify Checksums: all OK.
- [ ] Right-click the column header > Checksums > MD5: it fills in for each file.
- [ ] A folder with Thumbs.db or .DS_Store: they do not show until Ctrl+H. Tools > Clean Junk Files lists and trashes them.
- [ ] Select a file over 4 GB (or any file), Tools > Split File..., keep `fat32` (or type 100M): pieces .001, .002... appear. Select the .001, Tools > Join Pieces: the whole file comes back (compare with Checksums).
- [ ] Set DUAL horizontal. Right-click your own toolbar > Between the Displays, then right-click it > Same Row as the Toolbar Before: it runs on after the drives. Drag it by an empty spot onto another row and back.
- [ ] Window > Customize Toolbars > Commands > Toolbar pieces: drag "Copy Files ▾" onto a toolbar. Click it: copies; click its arrow: the menu. Drag a Spacer in front of a button: that button goes to the far end. Drag a Filter box in and type `*.png`.
- [ ] In customize mode, right-click a toolbar > Button Style > Icons Only, and Large Icons. Right-click a toolbar (outside customize mode) > In the Menu Bar.
- [ ] Window > Customize Toolbars > Default Toolbars: Add This Toolbar on Tools; drag "Turn Left" from Images onto it. Then Use the Classic Arrangement.
- [ ] Preferences > Status Bar: tick "Use my own definition", Samples > Classic, OK. The status bars show counts with icons, SOURCE/DEST and a disk bar. Codes menu > Disk space > Percent used adds {dp}.
- [ ] Turn on the trees (Set TREE=separate): dotted lines, + / - boxes, and a blue line (green in the destination) from the top down to the folder you are in.
- [ ] With Steam running, right-click a Windows .exe (or an AppImage) > Add to Steam: Steam adds it to the library (on the Deck it shows in Game Mode too). A text file has no Add to Steam.

## Right-click menu

- [ ] Preferences > Right-Click Menu > New: name "Run in VICE", shows for `*.d64;*.prg`, command `Go /downloads`. Right-click a .d64: it is near the top and goes to Downloads; a .png does not have it.
- [ ] Add one for Disk images in a submenu "ROMs": right-click an .iso, the ROMs submenu has it. Untick "Make Link Here" under Built-in items: it is gone from the menu.

## Tab groups, filters, notices

- [ ] Open a few tabs on both sides. Go > Tab Groups > Save These Tabs as a Group, name it. Close and change tabs, then Go > Tab Groups > your group: they all come back.
- [ ] Preferences > Tab Groups: untick "Close the open tabs first", OK. Open the group again: its tabs are added to the open ones. Rename and delete it there too.
- [ ] Ctrl+F, click Saved > Disc images in a ROM folder: only .iso/.chd/.cue show. Type `*.txt;*.nfo`, Saved > Save This Filter: it is in View > Saved Filters.
- [ ] Copy a big folder (over 10 seconds) to the SD card and switch to another app: a desktop notice says it is done. Click it: DirDuo opens that folder.

## Keyboard shortcuts

- [ ] File > Keyboard Shortcuts: click Edit > Invert Selection, click in Key and press Ctrl+K. OK. Ctrl+K now inverts the selection; Ctrl+I does nothing.
- [ ] Give Ctrl+K to another command too: it asks to take it from Invert Selection. Say No: both rows show the key in red. Then click Default on Invert Selection.
- [ ] New Hotkey...: name it Downloads, key F9, command `Go /downloads`. OK. F9 goes to Downloads. Untick it: F9 does nothing.
- [ ] More > Save a List of All Keys: open the CSV; every key is there.

## The viewer

- [ ] Preferences > Viewer: tick "Double-clicking a picture opens it in the viewer", OK. Double-click a screenshot: it opens in DirDuo's viewer. Space and Backspace step through the folder; F10 shows the information.
- [ ] In the viewer: = zooms in, F fits, G grows, R turns, H flips, O is 100%. Shift+drag a box, Ctrl+C, and paste into another app: just that part. Ctrl+Z puts it back.
- [ ] Press M on two screenshots: a star shows and the status bar counts them. Close the viewer: the "Marked Pictures" collection opens in a tab.
- [ ] Press S: a slideshow; S again stops it. Alt+Enter and Esc go in and out of full screen.
- [ ] Right-click an MP3 > Play: it plays in the viewer with a position bar and volume, and goes on to the next song when it ends. Space pauses; Left/Right seek; Up/Down change the volume.
- [ ] Right-click a video > Play: it plays; double-click it for full screen.
- [ ] Right-click a .txt or .nfo > View: the text shows; View > Hex View shows the bytes. A zip shows what is inside.
- [ ] In the viewer, File > Follow the Lister, then arrow through files in the lister: the viewer follows.

## Scripts

- [ ] Tools > Scripts > Manage Scripts..., Add Examples: three scripts show as OK, with what each adds.
- [ ] Open a ROM folder, right-click a column header > Scripts > Region: (USA), (Europe), (Japan) fill in. Ctrl+Shift+P, `SelectRegion REGION=Japan`: the Japanese ones are selected.
- [ ] Select some MP3s, `MakePlaylist`: a .m3u appears with them in it.
- [ ] Open a few folders, then `RecentFolders`: it lists them; pick one and it opens. Quit and reopen: the list is still there.
- [ ] Tools > Scripts > New Script..., name it `Hello`: it opens in the editor. Save it as is: `Hello` runs from the console and lists the selection. Add a typo and save: the Scripts page shows the error and its line; fix it and save: OK again.
- [ ] Untick a script on the Scripts page: its command and column go away; tick it: they are back.

## Report back

For anything that fails: what you clicked, what happened, and a screenshot if it is visual.
