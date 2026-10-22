#pragma once

#include <QString>

// Where Linopus keeps its files. Inside a Flatpak the XDG variables point into
// ~/.var/app/<id>/, so these resolve against the real home directory instead:
// the spec keeps config portable in ~/.config/linopus and the trash must be the
// one every other file manager on the machine uses.
namespace Paths {

bool insideFlatpak();

// ~/.config (or $XDG_CONFIG_HOME outside a Flatpak).
QString userConfigHome();

// ~/.local/share (or $XDG_DATA_HOME outside a Flatpak).
QString userDataHome();

// ~/.cache (or $XDG_CACHE_HOME outside a Flatpak): the shared thumbnail
// cache lives in its thumbnails folder.
QString userCacheHome();

// ~/.config/linopus, created on demand.
QString configDir();

// Full path of a file inside configDir().
QString configFile(const QString &name);

// Can be pointed somewhere else by tests.
void setConfigDirOverride(const QString &dir);
void setDataHomeOverride(const QString &dir);
void setCacheHomeOverride(const QString &dir);

} // namespace Paths
