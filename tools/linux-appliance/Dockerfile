FROM debian:trixie
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    git parted quilt coreutils debootstrap zerofree zip dosfstools e2fsprogs \
    libarchive-tools libcap2-bin rsync grep udev xz-utils curl xxd file kmod bc \
    ca-certificates fdisk gpg pigz arch-test bmap-tools qemu-user-static \
    && rm -rf /var/lib/apt/lists/*
ARG PIGEN_REV=86919dae864359499d8148a49910a558d49a00f1
RUN git init /pi-gen && cd /pi-gen \
    && git remote add origin https://github.com/RPi-Distro/pi-gen.git \
    && git fetch --depth 1 origin "$PIGEN_REV" && git checkout --detach FETCH_HEAD \
    && touch stage2/SKIP_IMAGES
WORKDIR /pi-gen
