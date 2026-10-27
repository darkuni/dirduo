#!/bin/sh
# Builds DirDuo as a Flatpak and installs it for this user. Runs on SteamOS in
# Desktop Mode (Konsole) without touching the read-only system: the build tool
# and the KDE SDK are themselves Flatpaks from Flathub.
#
#   sh scripts/build-flatpak-on-deck.sh
#
# The first run downloads the KDE SDK (about 1 GB). Later runs are quick.
set -e
cd "$(dirname "$0")/.."

flatpak remote-add --user --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install --user -y --noninteractive flathub org.flatpak.Builder org.kde.Sdk//6.10 org.kde.Platform//6.10

flatpak run org.flatpak.Builder --user --install --force-clean --install-deps-from=flathub \
    flatpak-build flatpak/com.monroeworld.DirDuo.yml

echo
echo "Installed. Start it from the application menu (DirDuo), or:"
echo "  flatpak run com.monroeworld.DirDuo"
