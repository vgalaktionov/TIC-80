#!/bin/bash -e
install -d "${DEPLOY_DIR}/smoke-test"
cp -a "${ROOTFS_DIR}/tmp/tic80-smoke/." "${DEPLOY_DIR}/smoke-test/"
rm -rf "${ROOTFS_DIR}/tmp/tic80-smoke" "${ROOTFS_DIR}/tmp/tic80-smoke.py"
