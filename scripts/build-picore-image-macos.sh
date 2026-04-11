#!/bin/sh

set -eu

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ARTIFACT_DIR="${ARTIFACT_DIR:-$ROOT_DIR/build/tinycore/artifacts}"
WORK_DIR="${WORK_DIR:-$ROOT_DIR/build/tinycore/image}"
CACHE_DIR="${CACHE_DIR:-$ROOT_DIR/build/tinycore/cache}"
OUTPUT_DIR="${OUTPUT_DIR:-$ROOT_DIR/out}"
TARGET_ARCH="${TARGET_ARCH:-aarch64}"
TC_VERSION="${TC_VERSION:-15.x}"
TCE_NAME="${TCE_NAME:-tce}"
WIFI_SSID="${WIFI_SSID:-}"
WIFI_PSK="${WIFI_PSK:-}"
WIFI_COUNTRY="${WIFI_COUNTRY:-IN}"
ALSA_CARD="${ALSA_CARD:-0}"
ALSA_DEVICE="${ALSA_DEVICE:-0}"
ENABLE_HDMI_AUDIO="${ENABLE_HDMI_AUDIO:-0}"
RELEASE_BASE_URL="${RELEASE_BASE_URL:-}"
EXT_BASE_URL="${EXT_BASE_URL:-}"
RELEASE_IMAGE="${RELEASE_IMAGE:-}"
AUDIO_MODULES_EXT="${AUDIO_MODULES_EXT:-}"
WIRELESS_MODULES_EXT="${WIRELESS_MODULES_EXT:-}"

case "$TARGET_ARCH" in
    aarch64)
        IMAGE_PREFIX="piCore64"
        KERNEL_PATTERN="piCore-v8"
        ;;
    armhf)
        IMAGE_PREFIX="piCore"
        KERNEL_PATTERN="piCore"
        ;;
    *)
        printf 'Unsupported TARGET_ARCH: %s\n' "$TARGET_ARCH" >&2
        exit 1
        ;;
esac

RELEASE_BASE_URL_DEFAULT="http://tinycorelinux.net/${TC_VERSION}/${TARGET_ARCH}/release/RPi/"
EXT_BASE_URL_DEFAULT="http://repo.tinycorelinux.net/${TC_VERSION}/${TARGET_ARCH}/tcz/"
RELEASE_BASE_URL="${RELEASE_BASE_URL:-$RELEASE_BASE_URL_DEFAULT}"
EXT_BASE_URL="${EXT_BASE_URL:-$EXT_BASE_URL_DEFAULT}"
RELEASE_BASE_URL_RESOLVED=""
EXT_BASE_URL_RESOLVED=""
RELEASE_IMAGE_DISCOVERED=""
RELEASE_VERSION_RESOLVED=""
AUDIO_MODULES_EXT_DISCOVERED=""
WIRELESS_MODULES_EXT_DISCOVERED=""
EXT_CACHE_DIR=""
OUTPUT_IMAGE="${OUTPUT_IMAGE:-$OUTPUT_DIR/custom-picore-rpi3-${TARGET_ARCH}.img}"

require_file() {
    [ -f "$1" ] || {
        printf 'Missing required file: %s\n' "$1" >&2
        exit 1
    }
}

download_if_missing() {
    url="$1"
    dest="$2"
    mkdir -p "$(dirname "$dest")"
    if [ ! -f "$dest" ]; then
        curl --retry 5 --retry-delay 2 --retry-connrefused -fL "$url" -o "$dest"
    fi
}

copy_cached_or_download() {
    url="$1"
    cache_path="$2"
    dest="$3"

    mkdir -p "$(dirname "$cache_path")" "$(dirname "$dest")"

    if [ ! -f "$cache_path" ]; then
        curl --retry 5 --retry-delay 2 --retry-connrefused -fL "$url" -o "$cache_path"
    fi

    cp "$cache_path" "$dest"
}

sanitize_dep_file() {
    dep_file="$1"
    [ -f "$dep_file" ] || return 0

    if [ -n "$AUDIO_MODULES_EXT" ]; then
        sed -i '' "s/^alsa-modules-KERNEL\.tcz$/${AUDIO_MODULES_EXT}/" "$dep_file" 2>/dev/null || true
    fi
    if [ -n "$WIRELESS_MODULES_EXT" ]; then
        sed -i '' "s/^wireless-KERNEL\.tcz$/${WIRELESS_MODULES_EXT}/" "$dep_file" 2>/dev/null || true
    fi
}

resolve_extension_name() {
    ext="$1"

    case "$ext" in
        alsa-modules-KERNEL.tcz)
            [ -n "$AUDIO_MODULES_EXT" ] && {
                printf '%s\n' "$AUDIO_MODULES_EXT"
                return 0
            }
            ;;
        wireless-KERNEL.tcz)
            [ -n "$WIRELESS_MODULES_EXT" ] && {
                printf '%s\n' "$WIRELESS_MODULES_EXT"
                return 0
            }
            ;;
    esac

    if curl -fsI "${EXT_BASE_URL}${ext}" >/dev/null 2>&1; then
        printf '%s\n' "$ext"
        return 0
    fi

    ext_index_cache="${EXT_CACHE_DIR}/index.html"
    if [ ! -f "$ext_index_cache" ]; then
        mkdir -p "$EXT_CACHE_DIR"
        curl -fsSL "$EXT_BASE_URL" -o "$ext_index_cache"
    fi

    resolved="$(awk -v want="$ext" '
        match($0, /[A-Za-z0-9._+-]+\.tcz/) {
            candidate = substr($0, RSTART, RLENGTH)
            if (tolower(candidate) == tolower(want)) {
                print candidate
                exit
            }
        }
    ' "$ext_index_cache")"

    [ -n "$resolved" ] || return 1
    printf '%s\n' "$resolved"
}

append_unique_lines() {
    src="$1"
    dest="$2"
    [ -f "$src" ] || return 0
    touch "$dest"
    while IFS= read -r line; do
        [ -n "$line" ] || continue
        if ! grep -Fqx "$line" "$dest"; then
            printf '%s\n' "$line" >> "$dest"
        fi
    done < "$src"
}

append_cmdline_tokens() {
    src="$1"
    dest="$2"
    [ -f "$src" ] || return 0
    current="$(cat "$dest")"
    while IFS= read -r token; do
        [ -n "$token" ] || continue
        case " $current " in
            *" $token "*) ;;
            *) current="$current $token" ;;
        esac
    done < "$src"
    printf '%s\n' "$current" > "$dest"
}

extract_hdiutil_value() {
    plist_file="$1"
    key="$2"
    index=0

    while :; do
        value="$(/usr/libexec/PlistBuddy -c "Print :system-entities:${index}:${key}" "$plist_file" 2>/dev/null || true)"
        if [ -n "$value" ]; then
            printf '%s\n' "$value"
            return 0
        fi
        next_exists="$(/usr/libexec/PlistBuddy -c "Print :system-entities:${index}" "$plist_file" 2>/dev/null || true)"
        [ -n "$next_exists" ] || break
        index=$((index + 1))
    done

    return 1
}

extract_boot_mount() {
    plist_file="$1"
    index=0

    while :; do
        entity_exists="$(/usr/libexec/PlistBuddy -c "Print :system-entities:${index}" "$plist_file" 2>/dev/null || true)"
        [ -n "$entity_exists" ] || break

        content_hint="$(/usr/libexec/PlistBuddy -c "Print :system-entities:${index}:content-hint" "$plist_file" 2>/dev/null || true)"
        mount_point="$(/usr/libexec/PlistBuddy -c "Print :system-entities:${index}:mount-point" "$plist_file" 2>/dev/null || true)"

        case "$content_hint" in
            DOS_FAT_32|Windows_FAT_32|MS-DOS*)
                if [ -n "$mount_point" ]; then
                    printf '%s\n' "$mount_point"
                    return 0
                fi
                ;;
        esac

        index=$((index + 1))
    done

    return 1
}

discover_release_image() {
    for base_url in \
        "$RELEASE_BASE_URL" \
        "http://tinycorelinux.net/17.x/${TARGET_ARCH}/release/RPi/" \
        "http://tinycorelinux.net/17.x/${TARGET_ARCH}/releases/RPi/" \
        "http://tinycorelinux.net/16.x/${TARGET_ARCH}/release/RPi/" \
        "http://tinycorelinux.net/16.x/${TARGET_ARCH}/releases/RPi/" \
        "http://tinycorelinux.net/15.x/${TARGET_ARCH}/release/RPi/" \
        "http://tinycorelinux.net/15.x/${TARGET_ARCH}/releases/RPi/" \
        "http://tinycorelinux.net/14.x/${TARGET_ARCH}/release/RPi/"
    do
        image="$(curl -fsSL "$base_url" 2>/dev/null | grep -Eo "${IMAGE_PREFIX}[^\" ]+\.(img\.gz|zip)" | head -n 1 || true)"
        if [ -n "$image" ]; then
            RELEASE_BASE_URL_RESOLVED="$base_url"
            RELEASE_VERSION_RESOLVED="$(printf '%s\n' "$base_url" | sed -E 's#.*tinycorelinux.net/([^/]+)/.*#\1#')"
            RELEASE_IMAGE_DISCOVERED="$image"
            return 0
        fi
    done

    return 1
}

discover_kernel_ext() {
    prefix="$1"
    for base_url in \
        "$EXT_BASE_URL" \
        "http://repo.tinycorelinux.net/17.x/${TARGET_ARCH}/tcz/" \
        "http://repo.tinycorelinux.net/16.x/${TARGET_ARCH}/tcz/" \
        "http://repo.tinycorelinux.net/15.x/${TARGET_ARCH}/tcz/" \
        "http://repo.tinycorelinux.net/14.x/${TARGET_ARCH}/tcz/"
    do
        page="$(curl -fsSL "$base_url" 2>/dev/null || true)"
        ext="$(printf '%s' "$page" | grep -Eo "${prefix}-[^\" ]+${KERNEL_PATTERN}[^\" ]*\.tcz" | sort -u | tail -n 1 || true)"
        if [ -n "$ext" ]; then
            EXT_BASE_URL_RESOLVED="$base_url"
            printf '%s\n' "$ext"
            return 0
        fi
    done

    return 1
}

download_extension_tree() {
    ext="$1"
    optional_dir="$2"

    case "$ext" in
        demo-examples-app.tcz)
            cp "$ARTIFACT_DIR/demo-examples-app.tcz" "$optional_dir/"
            cp "$ARTIFACT_DIR/demo-examples-app.tcz.dep" "$optional_dir/"
            cp "$ARTIFACT_DIR/demo-examples-app.tcz.info" "$optional_dir/"
            cp "$ARTIFACT_DIR/demo-examples-app.tcz.list" "$optional_dir/"
            return 0
            ;;
    esac

    repo_ext="$(resolve_extension_name "$ext")" || {
        printf 'Unable to resolve Tiny Core extension: %s from %s\n' "$ext" "$EXT_BASE_URL" >&2
        return 1
    }

    copy_cached_or_download "${EXT_BASE_URL}${repo_ext}" "$EXT_CACHE_DIR/$repo_ext" "$optional_dir/$repo_ext"

    if [ "$repo_ext" != "$ext" ] && [ ! -e "$optional_dir/$ext" ]; then
        cp "$optional_dir/$repo_ext" "$optional_dir/$ext"
    fi

    if curl -fsI "${EXT_BASE_URL}${repo_ext}.dep" >/dev/null 2>&1; then
        copy_cached_or_download "${EXT_BASE_URL}${repo_ext}.dep" "$EXT_CACHE_DIR/$repo_ext.dep" "$optional_dir/$repo_ext.dep"
        sanitize_dep_file "$optional_dir/$repo_ext.dep"
        if [ "$repo_ext" != "$ext" ] && [ ! -e "$optional_dir/$ext.dep" ]; then
            cp "$optional_dir/$repo_ext.dep" "$optional_dir/$ext.dep"
            sanitize_dep_file "$optional_dir/$ext.dep"
        fi
        while IFS= read -r dep; do
            [ -n "$dep" ] || continue
            download_extension_tree "$dep" "$optional_dir"
        done < "$optional_dir/$repo_ext.dep"
    fi

    if curl -fsI "${EXT_BASE_URL}${repo_ext}.md5.txt" >/dev/null 2>&1; then
        copy_cached_or_download "${EXT_BASE_URL}${repo_ext}.md5.txt" "$EXT_CACHE_DIR/$repo_ext.md5.txt" "$optional_dir/$repo_ext.md5.txt"
        if [ "$repo_ext" != "$ext" ] && [ ! -e "$optional_dir/$ext.md5.txt" ]; then
            cp "$optional_dir/$repo_ext.md5.txt" "$optional_dir/$ext.md5.txt"
        fi
    fi

    if curl -fsI "${EXT_BASE_URL}${repo_ext}.info" >/dev/null 2>&1; then
        copy_cached_or_download "${EXT_BASE_URL}${repo_ext}.info" "$EXT_CACHE_DIR/$repo_ext.info" "$optional_dir/$repo_ext.info"
        if [ "$repo_ext" != "$ext" ] && [ ! -e "$optional_dir/$ext.info" ]; then
            cp "$optional_dir/$repo_ext.info" "$optional_dir/$ext.info"
        fi
    fi
}

build_mydata_tgz() {
    mydata_root="$WORK_DIR/mydata-root"
    mydata_tgz="$WORK_DIR/mydata.tgz"

    rm -rf "$mydata_root"
    mkdir -p "$mydata_root/opt" "$mydata_root/etc" "$mydata_root/home/tc"

    cp "$ROOT_DIR/tinycore/overlay/opt/bootlocal.sh" "$mydata_root/opt/bootlocal.sh"
    cp "$ROOT_DIR/tinycore/overlay/opt/.filetool.lst" "$mydata_root/opt/.filetool.lst"

    cat > "$mydata_root/etc/asound.conf" <<EOF
pcm.!default {
    type plug
    slave.pcm "hw:${ALSA_CARD},${ALSA_DEVICE}"
}

ctl.!default {
    type hw
    card ${ALSA_CARD}
}
EOF

    if [ -n "$WIFI_SSID" ] && [ -n "$WIFI_PSK" ]; then
        cat > "$mydata_root/etc/wpa_supplicant.conf" <<EOF
ctrl_interface=/var/run/wpa_supplicant
update_config=1
country=${WIFI_COUNTRY}
network={
    ssid="${WIFI_SSID}"
    psk="${WIFI_PSK}"
    key_mgmt=WPA-PSK
    scan_ssid=1
}
EOF
    else
        : > "$mydata_root/etc/wpa_supplicant.conf"
    fi

    chmod 0755 "$mydata_root/opt/bootlocal.sh"
    (cd "$mydata_root" && tar -czf "$mydata_tgz" .)
    printf '%s\n' "$mydata_tgz"
}

require_file "$ARTIFACT_DIR/demo-examples-app.tcz"
require_file "$ARTIFACT_DIR/demo-examples-app.tcz.dep"
require_file "$ARTIFACT_DIR/demo-examples-app.tcz.info"
require_file "$ARTIFACT_DIR/demo-examples-app.tcz.list"
require_file "$ARTIFACT_DIR/onboot.lst"
require_file "$ARTIFACT_DIR/config.txt.append"
require_file "$ARTIFACT_DIR/cmdline.append"

mkdir -p "$WORK_DIR" "$CACHE_DIR" "$OUTPUT_DIR"

if [ -z "$RELEASE_IMAGE" ]; then
    discover_release_image || true
    RELEASE_IMAGE="${RELEASE_IMAGE_DISCOVERED:-}"
fi
[ -n "$RELEASE_IMAGE" ] || {
    printf 'Unable to discover a piCore image from %s\n' "$RELEASE_BASE_URL" >&2
    exit 1
}

if [ -n "$RELEASE_BASE_URL_RESOLVED" ]; then
    RELEASE_BASE_URL="$RELEASE_BASE_URL_RESOLVED"
fi
if [ -n "$RELEASE_VERSION_RESOLVED" ] && [ "$EXT_BASE_URL" = "$EXT_BASE_URL_DEFAULT" ]; then
    EXT_BASE_URL="http://repo.tinycorelinux.net/${RELEASE_VERSION_RESOLVED}/${TARGET_ARCH}/tcz/"
fi
EXT_CACHE_DIR="$CACHE_DIR/${RELEASE_VERSION_RESOLVED:-$TC_VERSION}-${TARGET_ARCH}/tcz"

if [ -z "$AUDIO_MODULES_EXT" ]; then
    AUDIO_MODULES_EXT="$(discover_kernel_ext 'alsa-modules' || true)"
fi
if [ -z "$WIRELESS_MODULES_EXT" ]; then
    WIRELESS_MODULES_EXT="$(discover_kernel_ext 'wireless' || true)"
fi
if [ -n "$EXT_BASE_URL_RESOLVED" ]; then
    EXT_BASE_URL="$EXT_BASE_URL_RESOLVED"
fi

printf '[build-image] Using piCore release %s from %s\n' "$RELEASE_IMAGE" "$RELEASE_BASE_URL"
printf '[build-image] Using Tiny Core extensions from %s\n' "$EXT_BASE_URL"
[ -n "$AUDIO_MODULES_EXT" ] && printf '[build-image] Using ALSA kernel modules extension %s\n' "$AUDIO_MODULES_EXT"
[ -n "$WIRELESS_MODULES_EXT" ] && printf '[build-image] Using wireless kernel modules extension %s\n' "$WIRELESS_MODULES_EXT"

ARCHIVE_PATH="$CACHE_DIR/$RELEASE_IMAGE"
EXTRACT_DIR="$WORK_DIR/extracted-${TC_VERSION}-${TARGET_ARCH}"
RAW_IMAGE=""

download_if_missing "${RELEASE_BASE_URL}${RELEASE_IMAGE}" "$ARCHIVE_PATH"
rm -rf "$EXTRACT_DIR"
mkdir -p "$EXTRACT_DIR"

case "$ARCHIVE_PATH" in
    *.img.gz)
        RAW_IMAGE="$EXTRACT_DIR/$(basename "$RELEASE_IMAGE" .gz)"
        gunzip -c "$ARCHIVE_PATH" > "$RAW_IMAGE"
        ;;
    *.zip)
        unzip -o "$ARCHIVE_PATH" -d "$EXTRACT_DIR" >/dev/null
        RAW_IMAGE="$(find "$EXTRACT_DIR" -maxdepth 2 -name '*.img' | sort | head -n 1)"
        ;;
    *.img)
        RAW_IMAGE="$EXTRACT_DIR/$(basename "$RELEASE_IMAGE")"
        cp "$ARCHIVE_PATH" "$RAW_IMAGE"
        ;;
    *)
        printf 'Unsupported release artifact: %s\n' "$ARCHIVE_PATH" >&2
        exit 1
        ;;
esac

[ -n "$RAW_IMAGE" ] && [ -f "$RAW_IMAGE" ] || {
    printf 'Unable to locate extracted piCore image for %s\n' "$ARCHIVE_PATH" >&2
    exit 1
}

cp "$RAW_IMAGE" "$OUTPUT_IMAGE"

ATTACH_PLIST="$(mktemp -t picore-attach.XXXXXX.plist)"
hdiutil attach -plist "$OUTPUT_IMAGE" > "$ATTACH_PLIST"
DISK_DEV="$(extract_hdiutil_value "$ATTACH_PLIST" "dev-entry" | head -n 1)"
BOOT_MOUNT="$(extract_boot_mount "$ATTACH_PLIST" || true)"
rm -f "$ATTACH_PLIST"

[ -n "$DISK_DEV" ] || {
    printf 'Failed to attach image: %s\n' "$OUTPUT_IMAGE" >&2
    exit 1
}
[ -n "$BOOT_MOUNT" ] || {
    hdiutil detach "$DISK_DEV" >/dev/null 2>&1 || true
    printf 'Failed to mount FAT boot partition from %s\n' "$OUTPUT_IMAGE" >&2
    exit 1
}

trap 'hdiutil detach "$DISK_DEV" >/dev/null 2>&1 || true' EXIT

OPTIONAL_DIR="$BOOT_MOUNT/$TCE_NAME/optional"
ONBOOT_FILE="$BOOT_MOUNT/$TCE_NAME/onboot.lst"
mkdir -p "$OPTIONAL_DIR"

cp "$(build_mydata_tgz)" "$BOOT_MOUNT/mydata.tgz"
cp "$ARTIFACT_DIR/onboot.lst" "$ONBOOT_FILE"

if [ -n "$AUDIO_MODULES_EXT" ] && ! grep -Fqx "$AUDIO_MODULES_EXT" "$ONBOOT_FILE"; then
    printf '%s\n' "$AUDIO_MODULES_EXT" >> "$ONBOOT_FILE"
fi
if [ -n "$WIRELESS_MODULES_EXT" ] && ! grep -Fqx "$WIRELESS_MODULES_EXT" "$ONBOOT_FILE"; then
    printf '%s\n' "$WIRELESS_MODULES_EXT" >> "$ONBOOT_FILE"
fi

sed -i '' '/^alsa-modules-KERNEL\.tcz$/d' "$ONBOOT_FILE" 2>/dev/null || true
sed -i '' '/^wireless-KERNEL\.tcz$/d' "$ONBOOT_FILE" 2>/dev/null || true

while IFS= read -r ext; do
    [ -n "$ext" ] || continue
    download_extension_tree "$ext" "$OPTIONAL_DIR"
done < "$ONBOOT_FILE"

append_unique_lines "$ARTIFACT_DIR/config.txt.append" "$BOOT_MOUNT/config.txt"
append_cmdline_tokens "$ARTIFACT_DIR/cmdline.append" "$BOOT_MOUNT/cmdline.txt"

if [ "$ENABLE_HDMI_AUDIO" = "1" ]; then
    if ! grep -Fqx 'hdmi_force_hotplug=1' "$BOOT_MOUNT/config.txt"; then
        printf '%s\n' 'hdmi_force_hotplug=1' >> "$BOOT_MOUNT/config.txt"
    fi
    if ! grep -Fqx 'hdmi_drive=2' "$BOOT_MOUNT/config.txt"; then
        printf '%s\n' 'hdmi_drive=2' >> "$BOOT_MOUNT/config.txt"
    fi
fi

sync
hdiutil detach "$DISK_DEV" >/dev/null
trap - EXIT

gzip -f -c "$OUTPUT_IMAGE" > "${OUTPUT_IMAGE}.gz"

printf 'Custom piCore image created at %s\n' "$OUTPUT_IMAGE"
printf 'Compressed piCore image created at %s.gz\n' "$OUTPUT_IMAGE"
