#!/bin/bash

cd /home/orcaslicer || exit

rm -rf build
rm -rf deps/build

exec ./BuildLinux.sh -dsir
