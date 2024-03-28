#!/usr/bin/env bash

rm -rf _build ; mkdir _build
rm -rf _repo ; mkdir _repo

BRANCH=test

powerprofilesctl launch flatpak-builder --ccache --force-clean --default-branch=$BRANCH _build com.bambulab.BambuStudio.yml --repo=_repo
