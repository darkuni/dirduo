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
- [ ] Ctrl+F filter: type `*.png` and only PNGs stay; Esc clears it.
- [ ] Sizes button on a big folder (Emulation, for example): sizes and bars fill in without freezing.
- [ ] Flat button on a folder with subfolders: everything below, grouped by folder.
- [ ] A folder full of screenshots opens in icon view by itself.

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
- [ ] Take a screenshot with Spectacle and copy it to the clipboard, then Ctrl+V in a folder: a PNG appears.
- [ ] Select a few files, Shift+F2: Advanced Rename shows the before and after; rename, then Edit > Undo Rename.
- [ ] Window > Customize Toolbars: drag a button to another spot, double-click one to edit, close.
- [ ] File > Settings > Back Up Settings: a zip lands where you choose.
- [ ] Tools > Synchronize between a folder on internal storage and one on the SD card: the list makes sense; Synchronize; Compare again says they match.
- [ ] Ctrl+Shift+F, name `*.d64` (or any type you have), Find: results open in a tab; copy one of them to the other display.
- [ ] Select a few files, Edit > Add to Collection > New Collection; Go > Collections opens it; Delete there leaves the real files alone.
- [ ] Tools > Find Duplicates on a ROM folder: groups show; check copies, then Move Checked to Trash.

## Report back

For anything that fails: what you clicked, what happened, and a screenshot if it is visual.
