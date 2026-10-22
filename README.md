# Linopus

A source/destination file manager for Linux in the spirit of Windows Directory Opus. Two displays, one SOURCE and one DEST, and every command acts from one to the other.

Targets: SteamOS Desktop Mode on the Steam Deck (x86_64) and Steam Frame (aarch64), and any Linux desktop after that. Qt 6 Widgets, C++20, shipped as a Flatpak on the KDE runtime.

## Phase 1 (this build)

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

FTP/SFTP and archives-as-folders arrive with the KIO backend (see "Phase 1 gaps" below).

## Commands

Every button, menu item, hotkey, drop and inline rename runs one of these. Button functions can hold several lines; they run in order and stop at the first failure.

| Command | Arguments |
| --- | --- |
| `Go` | `<path or /alias>`, `UP`, `BACK`, `FORWARD`, `REFRESH`, `SWAP`, `NEWTAB`, `OPENINDEST` |
| `Copy` | `MOVE`, `AS`, `FILE=...`, `TO=...`, `WHENEXISTS=ask\|replace\|newer\|skip\|rename` |
| `Move` | same as `Copy MOVE` |
| `Delete` | `NORECYCLE` (permanent, asks first), `QUIET`, `FILE=...` |
| `Rename` | no arguments: rename in place; or `FROM=... TO=...` |
| `CreateFolder` | `NAME="A, B, C"` makes all three; no name asks; `IN=...` |
| `Clipboard` | `COPY`, `CUT`, `PASTE` |
| `Set` | `DUAL=on\|off\|toggle\|vertical\|horizontal\|flip`, `SOURCE=toggle`, `DESTLOCK=on\|off\|toggle`, `NAVLOCK=on\|off\|toggle`, `VIEW=details\|list\|icons`, `HIDDEN=on\|off\|toggle` |
| `Select` | `ALL`, `NONE`, `INVERT`, `PATTERN=*.mp3` |
| `Toolbar` | `NAME=buttons\|drives`, `PLACE=top\|bottom\|left\|right\|between`, `NEWROW`, `STATE=on\|off\|toggle` |

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
| Del / Shift+Del | Trash / delete permanently |
| Ctrl+X / C / V | Cut / copy / paste files |
| Ctrl+Shift+N | New folder(s) |
| Ctrl+T / Ctrl+W | New tab / close tab |
| Ctrl+Tab | Next tab |
| Ctrl+1 / 2 / 3 | Details / list / icons |
| Ctrl+H | Hidden files |
| Ctrl+A, Ctrl+Shift+A, Ctrl+I | Select all, none, invert |
| Ctrl+Shift+S | Select by pattern |
| F5 | Refresh |
| Ctrl+N | New lister |
| Ctrl+Shift+V / H / 1 | Dual vertical / dual horizontal / single |
| Ctrl+J | Show file operation windows |

## Toolbar file

Phase 1 reads `~/.config/linopus/toolbars/default.json` when it exists and uses the built-in set otherwise. The editor arrives in phase 3.

```json
{
  "buttons": [
    { "label": "Copy", "icon": "edit-copy", "left": "Copy", "right": "Copy AS" },
    { "separator": true },
    { "label": "Podcasts", "left": "Go /pods", "middle": "Go /pods NEWTAB" }
  ]
}
```

## Files

- `~/.config/linopus/linopus.ini`: session and window layout
- `~/.config/linopus/aliases.ini`: your aliases
- `~/.config/linopus/toolbars/`: button bars

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
QT_QPA_PLATFORM=offscreen ./build/linopus_snapshot /tmp/linopus-shots
```

`linopus_snapshot` builds a sample folder tree, drives the lister through a few commands and saves a PNG at each step.

## Phase 1 gaps

- **FTP and SFTP**: the spec puts remote folders on KIO. The build machine for this first pass had no KDE Frameworks 6 packages, so phase 1 runs on a local filesystem layer. Remote access and archives-as-folders land next, on KIO, in the Flatpak build where KDE Frameworks 6 is part of the runtime.
- **Error handling inside a job** skips and reports the item that failed (listed when the job finishes) rather than asking Retry/Skip/Abort per file.
- **A single display uses the most recently used other lister as its destination**, as in Opus; with only one lister open, Copy asks for a folder.
