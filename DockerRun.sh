#!/bin/bash
set -x
# Just in case, here's some other things that might help:
#  Force the container's hostname to be the same as your workstation
#  -h $HOSTNAME \
#  just give it all privileges if there's a wierd error
#  --privileged=true \
#  If there's problems with the X display, try this
#  -v /tmp/.X11-unix:/tmp/.X11-unix \
docker run \
  `# Use the hosts networking.  Printer wifi and also dbus communication` \
  --net=host \
  `# Run as your workstations username to keep permissions the same` \
  -u $USER \
  `# Bind mount your home directory into the container for loading/saving files` \
  -v $HOME:/home/$USER \
  `# Pass the X display number to the container` \
  -e DISPLAY=$DISPLAY \
  `# Attach tty for running bambu with command line things` \
  -ti \
  `# Pass all parameters from this script to the bambu ENTRYPOINT binary` \
  bambustudio $* 
  
