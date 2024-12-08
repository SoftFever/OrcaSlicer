#! /bin/bash

sudo apt update
sudo apt install build-essential flatpak flatpak-builder gnome-software-plugin-flatpak -y
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.gnome.Platform//46 org.gnome.Sdk//46

mkdir orcaslicer-build
cd orcaslicer-build

git clone https://github.com/SoftFever/OrcaSlicer.git
cd OrcaSlicer

flatpak-builder --force-clean build-dir flatpak/io.github.softfever.OrcaSlicer.yml