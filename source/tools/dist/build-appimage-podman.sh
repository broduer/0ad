#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later

podman build -t 0ad-debian-13 -f build/jenkins/dockerfiles/debian-13.Dockerfile
podman build -t 0ad-debian-13-appimage --build-context debian-13=container-image://0ad-debian-13 -f build/jenkins/dockerfiles/debian-13-appimage.Dockerfile

podman run --mount=type=bind,source=.,destination=/mnt --workdir /mnt 0ad-debian-13-appimage source/tools/dist/build-appimage.sh "$@"
