# Build Bambu Slicer in a container
#
# Build an AppImage:
# rm -rf build; sudo podman build .. -t bambu-studio  &&  sudo podman run --rm localhost/bambu-studio /bin/bash -c 'tar -c $(find build | grep ubu64.AppImage | head -1)' | tar -xv
#
# Troubleshooting:
# sudo podman run -it localhost/bambu-studio /bin/bash

FROM docker.io/ubuntu:kinetic
LABEL maintainer "DeftDawg <DeftDawg@gmail.com>"

# Add a deb-src
RUN echo deb-src http://archive.ubuntu.com/ubuntu \
    $(cat /etc/*release | grep VERSION_CODENAME | cut -d= -f2) main universe>> /etc/apt/sources.list 

RUN apt-get update && apt-get install  -y \
    git \
    build-essential \
    autoconf \
    cmake \
    libglu1-mesa-dev \
    libgtk-3-dev \
    libdbus-1-dev \
    curl \
    wget \
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
