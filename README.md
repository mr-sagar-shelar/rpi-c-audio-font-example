# TinyCore Raspberry Pi Build

This repo is now structured for a growing collection of standalone C examples:

- every file in `examples/*.c` becomes its own executable
- shared ALSA/audio helpers live under `include/` and `src/`
- Docker cross-compiles the executables and packages them into a TinyCore extension artifact
- a macOS script downloads piCore, resolves TinyCore dependencies, and assembles the final SD-card image locally with `hdiutil`

## Repo Layout

- [examples/](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/examples) contains one standalone example program per `.c` file
- [include/](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/include) contains reusable headers
- [src/](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/src) contains shared implementation used by multiple examples
- [tinycore/](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/tinycore) contains TinyCore packaging, overlays, and boot assets

## Build Executables

Build only the cross-compiled Raspberry Pi executables with Docker:

```bash
TARGET_ARCH="aarch64" \
bash /Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-executables.sh
```

This exports one executable per example source into:

- `build/tinycore/artifacts/bin/first`
- `build/tinycore/artifacts/bin/audio_output_selector`
- `build/tinycore/artifacts/bin/lcd_hat_menu`
- `build/tinycore/artifacts/bin/pirate_audio_hat`
- `build/tinycore/artifacts/bin/second`
- `build/tinycore/artifacts/bin/third`
- `build/tinycore/artifacts/bin/ups_hat_c_status`
- `build/tinycore/artifacts/bin/wm8960_tones`

Any new file you add under `examples/`, such as `examples/fourth.c`, will automatically produce `build/tinycore/artifacts/bin/fourth`.

## Build SD Card Image

Build the executables, TinyCore extension artifacts, and the final flashable piCore image on macOS:

```bash
WIFI_SSID="YourWiFi" \
WIFI_PSK="YourPassword" \
WIFI_COUNTRY="IN" \
ALSA_CARD="0" \
ALSA_DEVICE="0" \
TARGET_ARCH="aarch64" \
bash /Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-artifacts.sh
```

The final image is written to [out/](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/out).

For the Pirate Audio speaker HAT, build with its hardware profile enabled:

```bash
HARDWARE_PROFILE="pirate-audio-speaker" \
TARGET_ARCH="aarch64" \
bash /Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-artifacts.sh
```

For the Waveshare WM8960 Audio HAT, build with its hardware profile enabled:

```bash
HARDWARE_PROFILE="wm8960-audio-hat" \
TARGET_ARCH="aarch64" \
bash /Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-artifacts.sh
```

For the Waveshare UPS HAT (C), build with its hardware profile enabled:

```bash
HARDWARE_PROFILE="ups-hat-c" \
TARGET_ARCH="aarch64" \
bash /Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-artifacts.sh
```

## Output

The build creates:

- `build/tinycore/artifacts/bin/<example-name>`
- `build/tinycore/artifacts/demo-examples-app.tcz`
- `build/tinycore/artifacts/demo-examples-app.tcz.dep`
- `build/tinycore/artifacts/examples.manifest`
- `build/tinycore/artifacts/onboot.lst`
- `out/custom-picore-rpi3-aarch64.img`
- `out/custom-picore-rpi3-aarch64.img.gz`
- `out/custom-picore-rpi3-wm8960-audio-hat-aarch64.img`
- `out/custom-picore-rpi3-lcd-hat-aarch64.img`

Set `TARGET_ARCH=armhf` if you prefer 32-bit piCore.
If `HARDWARE_PROFILE` is set, the generated image name includes that profile.

## What Boots

The generated image:

- loads the packaged demo extension and its ALSA/WiFi dependencies from `/tce/optional`
- enables Raspberry Pi audio in `config.txt`
- applies a generated `asound.conf`
- starts WiFi on `wlan0` if `WIFI_SSID` and `WIFI_PSK` are provided
- launches a generated menu on `tty1` based on the discovered example executables

Each example is available on the Raspberry Pi as its own executable under:

- `/usr/local/bin/<example-name>`
- `/usr/local/examples/bin/<example-name>`

## Audio Output Selector Example

The new `audio_output_selector` example is a simple audio-routing validation tool.

It lets the user choose one of these output targets:

- HDMI
- 3.5mm audio jack
- WM8960 I2S HAT
- custom ALSA device name

After selecting an output, it plays the same melody on the chosen ALSA path so you can quickly validate routing on TinyCore.

For HDMI and 3.5mm jack, it attempts to switch the Raspberry Pi legacy output route using `amixer cset numid=3`.

## LCD HAT Example

The new `lcd_hat_menu` example targets the common Waveshare-style 1.44 inch LCD HAT with:

- ST7735S 128x128 SPI display
- 3 push buttons: `KEY1`, `KEY2`, `KEY3`
- joystick directions: `UP`, `DOWN`, `LEFT`, `RIGHT`, `PRESS`

When `lcd_hat_menu` runs on the Raspberry Pi, it:

- initializes the LCD over `/dev/spidev0.0`
- shows sample text on the screen
- updates the display with the last button name pressed on the HAT

The TinyCore image build now appends these boot settings automatically:

- `dtparam=spi=on`
- `gpio=6,19,5,26,13,21,20,16=pu`

Those pull-ups match the common 1.44 inch LCD HAT button wiring used by Waveshare's documentation.

## Pirate Audio Speaker Example

The new `pirate_audio_hat` example targets Pimoroni's Pirate Audio speaker board with display and buttons.

When `pirate_audio_hat` runs on the Raspberry Pi, it:

- draws sample text on the Pirate Audio display
- shows the last button pressed: `BUTTON A`, `BUTTON B`, `BUTTON X`, or `BUTTON Y`
- plays a different tone for each button to demonstrate the speaker/audio path

Use `HARDWARE_PROFILE=pirate-audio-speaker` when building the TinyCore image so the Pi boot config also appends:

- `dtoverlay=hifiberry-dac`
- `gpio=25=op,dh`

## WM8960 Audio HAT Example

The new `wm8960_tones` example targets the Waveshare WM8960 Audio HAT.

When `wm8960_tones` runs on the Raspberry Pi, it:

- records microphone input for 5 seconds
- can play back the recorded audio through the speaker path
- includes a stereo tone demo that alternates output between left, right, and both channels

Use `HARDWARE_PROFILE=wm8960-audio-hat` when building the TinyCore image so the Pi boot config appends:

- `dtoverlay=wm8960-soundcard`
- `dtparam=i2c_arm=on`

Important limitation:
Waveshare's own wiki and driver repository indicate that this HAT depends on their `wm8960-soundcard` driver/overlay stack. This repo now includes the example program and TinyCore profile hook, but a fully working TinyCore image may still require integrating the Waveshare driver package or matching overlay/module files for the exact Raspberry Pi kernel used by the piCore image.

## UPS HAT C Example

The new `ups_hat_c_status` example targets the Waveshare UPS HAT (C).

When `ups_hat_c_status` runs on the Raspberry Pi, it:

- reads bus voltage and current from the onboard INA219 over I2C
- estimates battery percentage from measured voltage
- reports charging, discharging, or idle state from current direction

Use `HARDWARE_PROFILE=ups-hat-c` when building the TinyCore image so the Pi boot config appends:

- `dtparam=i2c_arm=on`

Important note:
Waveshare documents INA219-based monitoring for this HAT. The percentage shown by this C example is a practical voltage-based estimate, not a dedicated fuel-gauge reading from the board.

## Notes

- The image assembly script is macOS-specific because it uses `hdiutil` to mount and update the piCore image.
- The build automatically discovers all `examples/*.c` files, so future examples do not require Dockerfile edits.
- Example-specific extra sources can be attached with sidecar files such as [examples/lcd_hat_menu.mk](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/examples/lcd_hat_menu.mk), which keeps hardware-specific drivers from being linked into every example.
- The builder dynamically discovers the latest matching piCore release and kernel-specific ALSA/WiFi extensions for the selected TinyCore branch.
- TinyCore framebuffer console rendering may still be limited for complex Hindi/Devanagari shaping even though the UTF-8 demo binary is included.
- If your Raspberry Pi audio output is not `hw:0,0`, set `ALSA_CARD` and `ALSA_DEVICE` before building.

## Main Files

- [Makefile](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/Makefile)
- [scripts/build-executables.sh](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-executables.sh)
- [scripts/build-artifacts.sh](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-artifacts.sh)
- [scripts/build-picore-image-macos.sh](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/scripts/build-picore-image-macos.sh)
- [tinycore/docker/artifact-builder.Dockerfile](/Users/sagarshelar/fliteDemo/rpi-c-audio-font-example/tinycore/docker/artifact-builder.Dockerfile)
