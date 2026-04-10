# TinyCore Raspberry Pi Build

This repository now includes a Docker-based image builder that prepares a custom piCore image for Raspberry Pi 3 Model B with:

- the three demo programs cross-compiled for Raspberry Pi
- ALSA runtime extensions and a default `asound.conf`
- Raspberry Pi WiFi extensions plus a `wpa_supplicant.conf` template
- automatic boot-time launch of a simple menu on `tty1`

## What It Produces

Running the builder creates:

- `out/custom-picore-rpi3-aarch64.img`
- `out/custom-picore-rpi3-aarch64.img.gz`
- `out/build-summary.txt`

`TARGET_ARCH=armhf` is also supported if you prefer the 32-bit piCore image.

## Quick Start

Build the Docker image:

```bash
docker compose build
```

Generate the custom piCore image:

```bash
WIFI_SSID="YourWiFi" \
WIFI_PSK="YourPassword" \
WIFI_COUNTRY="IN" \
ALSA_CARD="0" \
ALSA_DEVICE="0" \
docker compose run --rm picore-builder
```

The output image appears under [out/](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/out).

## Common Options

You can override these environment variables when running the builder:

- `TARGET_ARCH=aarch64` for Raspberry Pi 3 64-bit output
- `TARGET_ARCH=armhf` for Raspberry Pi 3 32-bit output
- `TINYCORE_MAJOR=16.x` to pin the TinyCore major release channel
- `WIFI_SSID`, `WIFI_PSK`, `WIFI_COUNTRY` to preseed WiFi
- `ALSA_CARD`, `ALSA_DEVICE` to set the default ALSA device used by `default`
- `ENABLE_HDMI_AUDIO=1` to append HDMI audio settings to `config.txt`

Example for 32-bit output:

```bash
TARGET_ARCH=armhf docker compose run --rm picore-builder
```

## Boot Behavior

At boot, the generated image:

- loads TinyCore ALSA and WiFi extensions from `/tce/onboot.lst`
- enables Raspberry Pi audio via `config.txt`
- starts `wpa_supplicant` and `udhcpc` if WiFi credentials were provided
- launches a menu on `tty1` to run `first`, `second`, or `third`

## Flashing the Image

On macOS, flash the generated image with Raspberry Pi Imager or `dd`.

Example using Raspberry Pi Imager:

1. Open Raspberry Pi Imager.
2. Choose `Use custom`.
3. Select `out/custom-picore-rpi3-aarch64.img.gz`.
4. Write the image to the SD card.

## Important Notes

- The build script dynamically picks the latest versioned piCore RPi image and matching kernel-specific TinyCore WiFi/ALSA extensions from the selected major branch.
- TinyCore console UTF-8 output is configured for your `third.c` demo, but raw Linux console rendering of Hindi can still be limited compared to a graphical terminal because Devanagari shaping support is not guaranteed in the framebuffer console.
- If your Raspberry Pi audio device is not card `0`, change `ALSA_CARD` and `ALSA_DEVICE` before building.
- If you do not want WiFi credentials baked into the image, omit `WIFI_SSID` and `WIFI_PSK`. You can later update `/etc/wpa_supplicant.conf` inside TinyCore and rebuild.

## Files Added

- [Dockerfile](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/Dockerfile)
- [docker-compose.yml](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/docker-compose.yml)
- [scripts/build-image.sh](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-image.sh)
