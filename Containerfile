# Build Bambu Slicer in a container
#
# Build an AppImage:
# rm -rf build; sudo podman build . -t bambu-studio  &&  sudo podman run --rm localhost/bambu-studio /bin/bash -c 'tar -c $(find build | grep ubu64.AppImage | head -1)' | tar -xv
#
# Troubleshooting the build container:
# sudo podman run -it --name bambu-studio localhost/bambu-studio /bin/bash
#
# Debugging the resulting AppImage:
#   1) Install `gdb`
#   2) In a terminal in the same directory as the AppImage, start it with following:
#      echo -e "run\nbt\nquit" | gdb ./BambuStudio_ubu64.AppImage
#   3) Find related issue using backtrace output for clues and add backtrace to it on github

FROM docker.io/ubuntu:20.04
LABEL maintainer "DeftDawg <DeftDawg@gmail.com>"

# Disable interactive package configuration
RUN apt-get update && \
    echo 'debconf debconf/frontend select Noninteractive' | debconf-set-selections

# Add a deb-src
RUN echo deb-src http://archive.ubuntu.com/ubuntu \
    $(cat /etc/*release | grep VERSION_CODENAME | cut -d= -f2) main universe>> /etc/apt/sources.list 

RUN apt-get update && apt-get install  -y \
    git \
    build-essential \
    autoconf pkgconf m4 \
    cmake extra-cmake-modules \
    libglu1-mesa-dev libglu1-mesa-dev \
    libwayland-dev libxkbcommon-dev wayland-protocols \
    eglexternalplatform-dev libglew-dev \
    libgtk-3-dev \
    libdbus-1-dev \
    libcairo2-dev \
    libgtk-3-dev libwebkit2gtk-4.0-dev \
    libsoup2.4-dev \
    libgstreamer1.0-dev libgstreamer-plugins-good1.0-dev libgstreamer-plugins-base1.0-dev libgstreamerd-3-dev \
    libmspack-dev \
    libosmesa6-dev \
    libssl-dev libcurl4-openssl-dev libsecret-1-dev \
    libudev-dev \
    curl \
    wget \
    file \
    sudo

COPY ../BambuStudio BambuStudio

WORKDIR BambuStudio

# These can run together, but we run them seperate for podman caching
# Update System dependencies
RUN ./BuildLinux.sh -u

# Build dependencies in ./deps
RUN ./BuildLinux.sh -d

# Build slic3r
RUN ./BuildLinux.sh -s

# Build AppImage
ENV container podman
RUN ./BuildLinux.sh -i
