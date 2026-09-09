#!/bin/bash
set -euo pipefail
rm -rf "${ROOTFS_DIR}/tmp/tic80-source"
install -d "${ROOTFS_DIR}/tmp/tic80-source"
git -C /source archive HEAD | tar -x -C "${ROOTFS_DIR}/tmp/tic80-source"
# Submodule sources are not included by git archive. Exclude local build products.
rsync -a --exclude=.git --exclude=/circle-stdlib --exclude=/mruby/build \
    --exclude=/janet/build --exclude='*.o' --exclude='*.a' \
    /source/vendor/ "${ROOTFS_DIR}/tmp/tic80-source/vendor/"
install -d "${ROOTFS_DIR}/usr/local/lib/tic80"
rsync -a --delete --exclude=__pycache__ /appliance/runtime/ "${ROOTFS_DIR}/usr/local/lib/tic80/"
install -m 600 /ssh-key.pub "${ROOTFS_DIR}/usr/local/lib/tic80/authorized_keys"
install -m 644 /appliance/tic80-wifi.nmconnection.example \
    "${ROOTFS_DIR}/boot/firmware/tic80-wifi.nmconnection.example"
install -m 644 /appliance/README.md "${ROOTFS_DIR}/boot/firmware/TIC80-README.txt"
install -m 644 /appliance/smoke.py "${ROOTFS_DIR}/tmp/tic80-smoke.py"
