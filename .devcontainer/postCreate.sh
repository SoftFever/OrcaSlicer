#!/bin/bash
# Update and upgrade all system packages
apt update
apt upgrade -y

sudo apt-get install -y cmake git g++ build-essential libgl1-mesa-dev m4 \
            libwayland-dev libxkbcommon-dev wayland-protocols extra-cmake-modules pkgconf \
            libglu1-mesa-dev libcairo2-dev libgtk-3-dev libsoup2.4-dev libwebkit2gtk-4.0-dev \
            libgstreamer1.0-dev libgstreamer-plugins-good1.0-dev libgstreamer-plugins-base1.0-dev \
            gstreamer1.0-plugins-bad libosmesa6-dev wget sudo autoconf curl libunwind-dev texinfo
		
./BuildLinux.sh -u