#!/bin/bash
PROJECT_ROOT=$(cd -P -- "$(dirname -- "$0")" && printf '%s\n' "$(pwd -P)")

set -x

# Wishlist hint:  For developers, creating a Docker Compose 
# setup with persistent volumes for the build & deps directories
# would speed up recompile times significantly.  For end users,
# the simplicity of a single Docker image and a one-time compilation
# seems better.
docker build -t bambustudio \
  --build-arg USER=$USER \
  --build-arg UID=$(id -u) \
  --build-arg GID=$(id -g) \
  $PROJECT_ROOT
