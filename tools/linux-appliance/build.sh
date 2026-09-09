#!/bin/bash
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT="$ROOT/build/linux-appliance"
SSH_PUBLIC_KEY_FILE=${SSH_PUBLIC_KEY_FILE:-$HOME/.ssh/id_ed25519.pub}
if [ ! -f "$SSH_PUBLIC_KEY_FILE" ]; then
    echo 'Set SSH_PUBLIC_KEY_FILE to your SSH public key file.' >&2
    exit 1
fi
if ! grep -Eq '^(ssh-|ecdsa-|sk-)' "$SSH_PUBLIC_KEY_FILE" \
    || grep -q 'PRIVATE KEY' "$SSH_PUBLIC_KEY_FILE"; then
    echo 'SSH_PUBLIC_KEY_FILE must contain an OpenSSH public key, not a private key.' >&2
    exit 1
fi
ssh-keygen -lf "$SSH_PUBLIC_KEY_FILE" >/dev/null
mkdir -p "$OUT"
AVAILABLE_KB=$(df -Pk "$OUT" | awk 'NR == 2 { print $4 }')
if [ "$AVAILABLE_KB" -lt 41943040 ]; then
    echo 'Image construction needs at least 40 GiB free on the host filesystem.' >&2
    exit 1
fi
case $(docker info --format '{{.Architecture}}') in
    aarch64|arm64) ;;
    *) echo 'This builder requires an ARM64 Docker engine.' >&2; exit 1 ;;
esac
# Public base images do not need the macOS keychain credential helper.
DOCKER_HOST=$(docker context inspect --format '{{.Endpoints.docker.Host}}') \
    DOCKER_CONFIG="$ROOT/tools/linux-appliance/docker-anonymous" \
    docker build -t tic80-appliance-builder "$ROOT/tools/linux-appliance"
docker volume create tic80-appliance-work >/dev/null
# Linux filesystem for debootstrap, not the macOS bind mount. No host disks mounted.
docker run --rm --name tic80-appliance-build --privileged \
    -v tic80-appliance-work:/pi-gen/work \
    -v "$OUT:/pi-gen/deploy" \
    -v "$ROOT:/source:ro" \
    -v "$ROOT/tools/linux-appliance:/appliance:ro" \
    -v "$SSH_PUBLIC_KEY_FILE:/ssh-key.pub:ro" \
    -e GIT_HASH="$(git -C "$ROOT" rev-parse HEAD)" \
    tic80-appliance-builder ./build.sh -c /appliance/config 2>&1 | tee "$OUT/build.log"
