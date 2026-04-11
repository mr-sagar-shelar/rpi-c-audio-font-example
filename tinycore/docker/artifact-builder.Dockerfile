FROM debian:bookworm-slim

ARG TARGET_ARCH=armhf
ARG INCLUDE_DEV_TOOLS=0

ENV DEBIAN_FRONTEND=noninteractive
WORKDIR /src

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        file \
        findutils \
        make \
        rsync \
        squashfs-tools \
        tar \
        unzip \
        zip \
    && rm -rf /var/lib/apt/lists/*

RUN case "${TARGET_ARCH}" in \
        armhf) \
            dpkg --add-architecture armhf \
            && apt-get update \
            && apt-get install -y --no-install-recommends \
                gcc-arm-linux-gnueabihf \
                binutils-arm-linux-gnueabihf \
                libc6-dev-armhf-cross \
                libasound2-dev:armhf \
            ;; \
        aarch64) \
            dpkg --add-architecture arm64 \
            && apt-get update \
            && apt-get install -y --no-install-recommends \
                gcc-aarch64-linux-gnu \
                binutils-aarch64-linux-gnu \
                libc6-dev-arm64-cross \
                libasound2-dev:arm64 \
            ;; \
        *) \
            echo "Unsupported TARGET_ARCH: ${TARGET_ARCH}" >&2; \
            exit 1; \
            ;; \
    esac \
    && rm -rf /var/lib/apt/lists/*

COPY . /src

RUN set -eux; \
    make clean; \
    make TARGET_ARCH="${TARGET_ARCH}" all; \
    rm -rf /tmp/demo-package /out; \
    mkdir -p /tmp/demo-package/usr/local/bin /tmp/demo-package/usr/local/examples/bin /tmp/demo-package/usr/local/share/demo-examples /out/bin /out/demo-workspace; \
    install -m 0755 /src/tinycore/rootfs/usr/local/bin/demo-menu.sh /tmp/demo-package/usr/local/bin/demo-menu.sh; \
    install -m 0755 /src/tinycore/rootfs/usr/local/bin/demo-launch-on-tty1.sh /tmp/demo-package/usr/local/bin/demo-launch-on-tty1.sh; \
    cp "/src/build/bin/${TARGET_ARCH}/examples.manifest" /tmp/demo-package/usr/local/share/demo-examples/examples.manifest; \
    while IFS= read -r example_name; do \
        [ -n "$example_name" ] || continue; \
        install -m 0755 "/src/build/bin/${TARGET_ARCH}/${example_name}" "/tmp/demo-package/usr/local/examples/bin/${example_name}"; \
        ln -sf "../examples/bin/${example_name}" "/tmp/demo-package/usr/local/bin/${example_name}"; \
        cp "/src/build/bin/${TARGET_ARCH}/${example_name}" "/out/bin/${example_name}"; \
    done < "/src/build/bin/${TARGET_ARCH}/examples.manifest"; \
    cp /src/Makefile /out/demo-workspace/Makefile; \
    mkdir -p /out/demo-workspace/examples /out/demo-workspace/include /out/demo-workspace/src; \
    cp -R /src/examples/. /out/demo-workspace/examples/; \
    cp -R /src/include/. /out/demo-workspace/include/; \
    cp -R /src/src/. /out/demo-workspace/src/; \
    mksquashfs /tmp/demo-package /out/demo-examples-app.tcz -noappend -all-root; \
    (cd /tmp/demo-package && find usr -not -type d | sort > /out/demo-examples-app.tcz.list); \
    cp "/src/build/bin/${TARGET_ARCH}/examples.manifest" /out/examples.manifest

RUN cat > /out/demo-examples-app.tcz.dep <<'EOF'
alsa.tcz
alsa-utils.tcz
alsa-plugins.tcz
alsa-modules-KERNEL.tcz
wireless_tools.tcz
wpa_supplicant.tcz
wifi.tcz
regdb.tcz
firmware-rpi-wifi.tcz
wireless-KERNEL.tcz
EOF

RUN if [ "${INCLUDE_DEV_TOOLS}" = "1" ]; then \
        printf '%s\n' \
            compiletc.tcz \
            gcc.tcz \
            make.tcz \
            alsa-dev.tcz >> /out/demo-examples-app.tcz.dep; \
    fi

RUN cat > /out/demo-examples-app.tcz.info <<'EOF'
Title:          demo-examples-app.tcz
Description:    TinyCore package containing the simple ALSA and UTF-8 Raspberry Pi demo programs from this repository.
Version:        0.1
Author:         Local project build
Original-site:  https://github.com/
Copying-policy: Mixed
Size:           custom
Extension_by:   Codex
Tags:           alsa unicode tinycore raspberrypi demo
Comments:       Includes all discovered example binaries plus a simple boot launcher.
Current:        2026/04/11 Initial local appliance package.
EOF

RUN cp /src/tinycore/extensions/onboot.lst /out/onboot.lst \
    && cp /src/tinycore/config/config.txt.append /out/config.txt.append \
    && cp /src/tinycore/config/cmdline.append /out/cmdline.append
