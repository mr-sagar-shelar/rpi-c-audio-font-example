FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive
ENV WORKSPACE=/workspace

RUN dpkg --add-architecture arm64 \
    && dpkg --add-architecture armhf \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        bash \
        bc \
        binutils \
        binutils-aarch64-linux-gnu \
        binutils-arm-linux-gnueabihf \
        bsdmainutils \
        build-essential \
        ca-certificates \
        coreutils \
        curl \
        dosfstools \
        e2fsprogs \
        e2tools \
        file \
        findutils \
        gcc-aarch64-linux-gnu \
        gcc-arm-linux-gnueabihf \
        gzip \
        jq \
        libc6-dev-arm64-cross \
        libc6-dev-armhf-cross \
        libasound2-dev:arm64 \
        libasound2-dev:armhf \
        mtools \
        parted \
        perl \
        python3 \
        rsync \
        sed \
        tar \
        unzip \
        xz-utils \
        zip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . /workspace

RUN chmod +x /workspace/scripts/build-image.sh

ENTRYPOINT ["/workspace/scripts/build-image.sh"]
