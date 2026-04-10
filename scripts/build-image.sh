#!/usr/bin/env bash

set -euo pipefail

WORKSPACE="${WORKSPACE:-/workspace}"
OUTPUT_DIR="${OUTPUT_DIR:-${WORKSPACE}/out}"
BUILD_ROOT="${BUILD_ROOT:-/tmp/picore-build}"

TARGET_ARCH="${TARGET_ARCH:-aarch64}"
TINYCORE_MAJOR="${TINYCORE_MAJOR:-16.x}"
WIFI_SSID="${WIFI_SSID:-}"
WIFI_PSK="${WIFI_PSK:-}"
WIFI_COUNTRY="${WIFI_COUNTRY:-IN}"
ALSA_CARD="${ALSA_CARD:-0}"
ALSA_DEVICE="${ALSA_DEVICE:-0}"
ENABLE_HDMI_AUDIO="${ENABLE_HDMI_AUDIO:-0}"

case "${TARGET_ARCH}" in
  aarch64)
    TARGET_BITS="64"
    TARGET_TRIPLE="aarch64-linux-gnu"
    TARGET_LIB_DIR="/usr/lib/aarch64-linux-gnu"
    IMAGE_PREFIX="piCore64"
    KERNEL_FLAVOR="piCore-v8"
    ;;
  armhf)
    TARGET_BITS="32"
    TARGET_TRIPLE="arm-linux-gnueabihf"
    TARGET_LIB_DIR="/usr/lib/arm-linux-gnueabihf"
    IMAGE_PREFIX="piCore"
    KERNEL_FLAVOR="piCore"
    ;;
  *)
    echo "Unsupported TARGET_ARCH '${TARGET_ARCH}'. Use 'aarch64' or 'armhf'." >&2
    exit 1
    ;;
esac

RELEASE_BASE_URL="https://tinycorelinux.net/${TINYCORE_MAJOR}/${TARGET_ARCH}/releases/RPi"
REPO_BASE_URL="https://repo.tinycorelinux.net/${TINYCORE_MAJOR}/${TARGET_ARCH}/tcz"
BIN_DIR="${BUILD_ROOT}/bin"
DOWNLOAD_DIR="${BUILD_ROOT}/downloads"
EXT_DIR="${BUILD_ROOT}/extensions"
ROOTFS_DIR="${BUILD_ROOT}/rootfs"
IMAGE_WORK_DIR="${BUILD_ROOT}/image"
STAGING_DIR="${BUILD_ROOT}/staging"

rm -rf "${BUILD_ROOT}"
mkdir -p "${OUTPUT_DIR}" "${BIN_DIR}" "${DOWNLOAD_DIR}" "${EXT_DIR}" "${ROOTFS_DIR}" "${IMAGE_WORK_DIR}" "${STAGING_DIR}"

log() {
  printf '[build-image] %s\n' "$*"
}

require_command() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Required command '$1' not found." >&2
    exit 1
  }
}

cleanup() {
  rm -rf "${BUILD_ROOT}"
}
trap cleanup EXIT

require_command curl
require_command jq
require_command unzip
require_command gunzip
require_command sfdisk
require_command dd
require_command mcopy
require_command debugfs

fetch_release_listing() {
  curl -fsSL "${RELEASE_BASE_URL}/"
}

fetch_repo_listing() {
  curl -fsSL "${REPO_BASE_URL}/"
}

select_release_asset() {
  local listing
  listing="$(fetch_release_listing)"

  local asset
  asset="$(
    printf '%s' "${listing}" \
      | grep -oE "${IMAGE_PREFIX}-[0-9][^\"']+\\.(zip|img\\.gz)" \
      | sort -uV \
      | tail -n1
  )"

  if [[ -z "${asset}" ]]; then
    asset="$(
      printf '%s' "${listing}" \
        | grep -oE "${IMAGE_PREFIX}-current\\.(zip|img\\.gz)" \
        | sort -u \
        | tail -n1
    )"
  fi

  if [[ -z "${asset}" ]]; then
    echo "Unable to find a piCore release asset at ${RELEASE_BASE_URL}/" >&2
    exit 1
  fi

  printf '%s' "${asset}"
}

select_kernel_extension() {
  local prefix="$1"
  local listing
  listing="$(fetch_repo_listing)"

  local asset
  asset="$(
    printf '%s' "${listing}" \
      | grep -oE "${prefix}-[0-9][^\"']+${KERNEL_FLAVOR}[^\"']*\\.tcz" \
      | sort -uV \
      | tail -n1
  )"

  if [[ -z "${asset}" ]]; then
    echo "Unable to find ${prefix} kernel extension in ${REPO_BASE_URL}/" >&2
    exit 1
  fi

  printf '%s' "${asset}"
}

download_file() {
  local url="$1"
  local destination="$2"

  mkdir -p "$(dirname "${destination}")"
  curl -fsSL "${url}" -o "${destination}"
}

download_extension_recursive() {
  local extension="$1"
  local local_name="${extension##*/}"

  if [[ -f "${EXT_DIR}/${local_name}" ]]; then
    return
  fi

  log "Downloading extension ${local_name}"
  download_file "${REPO_BASE_URL}/${local_name}" "${EXT_DIR}/${local_name}"

  for sidecar in .dep .md5.txt .info; do
    local sidecar_name="${local_name}${sidecar}"
    curl -fsSL "${REPO_BASE_URL}/${sidecar_name}" -o "${EXT_DIR}/${sidecar_name}" || true
  done

  if [[ -f "${EXT_DIR}/${local_name}.dep" ]]; then
    while IFS= read -r dep; do
      [[ -z "${dep}" ]] && continue
      download_extension_recursive "${dep}"
    done < "${EXT_DIR}/${local_name}.dep"
  fi
}

compile_programs() {
  local cc="${TARGET_TRIPLE}-gcc"
  local common_flags=(-O2 -Wall -Wextra -I/usr/include -L"${TARGET_LIB_DIR}")

  log "Cross-compiling demo binaries for ${TARGET_ARCH}"
  "${cc}" "${common_flags[@]}" -o "${BIN_DIR}/first" "${WORKSPACE}/first.c" -lm -lasound
  "${cc}" "${common_flags[@]}" -o "${BIN_DIR}/second" "${WORKSPACE}/second.c" -lm -lasound
  "${cc}" "${common_flags[@]}" -o "${BIN_DIR}/third" "${WORKSPACE}/third.c" -lm -lasound
}

extract_image() {
  local asset_name="$1"
  local asset_path="${DOWNLOAD_DIR}/${asset_name}"

  log "Downloading base piCore image ${asset_name}"
  download_file "${RELEASE_BASE_URL}/${asset_name}" "${asset_path}"

  case "${asset_name}" in
    *.zip)
      unzip -o "${asset_path}" -d "${IMAGE_WORK_DIR}" >/dev/null
      ;;
    *.img.gz)
      cp "${asset_path}" "${IMAGE_WORK_DIR}/base.img.gz"
      gunzip -f "${IMAGE_WORK_DIR}/base.img.gz"
      ;;
    *)
      echo "Unsupported release asset type: ${asset_name}" >&2
      exit 1
      ;;
  esac

  local img_path
  img_path="$(find "${IMAGE_WORK_DIR}" -maxdepth 1 -type f -name '*.img' | head -n1)"

  if [[ -z "${img_path}" ]]; then
    echo "No .img file found after extracting ${asset_name}" >&2
    exit 1
  fi

  printf '%s' "${img_path}"
}

get_partition_meta() {
  local img_path="$1"
  sfdisk -J "${img_path}" | jq -r '.partitiontable.partitions[] | [.node, (.start|tostring), (.size|tostring), .type] | @tsv'
}

extract_partitions() {
  local img_path="$1"
  local boot_image="${STAGING_DIR}/boot.fat"
  local root_image="${STAGING_DIR}/root.ext4"

  while IFS=$'\t' read -r node start size _type; do
    case "${node}" in
      *1)
        dd if="${img_path}" of="${boot_image}" bs=512 skip="${start}" count="${size}" status=none
        ;;
      *2)
        dd if="${img_path}" of="${root_image}" bs=512 skip="${start}" count="${size}" status=none
        ;;
    esac
  done < <(get_partition_meta "${img_path}")

  if [[ ! -f "${boot_image}" || ! -f "${root_image}" ]]; then
    echo "Failed to extract boot/root partitions from ${img_path}" >&2
    exit 1
  fi

  printf '%s\t%s' "${boot_image}" "${root_image}"
}

patch_config_txt() {
  local config_txt="${ROOTFS_DIR}/config.txt"

  if [[ ! -f "${config_txt}" ]]; then
    return
  fi

  if ! grep -q '^dtparam=audio=on' "${config_txt}"; then
    printf '\ndtparam=audio=on\n' >> "${config_txt}"
  fi

  if [[ "${ENABLE_HDMI_AUDIO}" == "1" ]]; then
    grep -q '^hdmi_force_hotplug=1' "${config_txt}" || printf 'hdmi_force_hotplug=1\n' >> "${config_txt}"
    grep -q '^hdmi_drive=2' "${config_txt}" || printf 'hdmi_drive=2\n' >> "${config_txt}"
  fi
}

patch_cmdline_txt() {
  local cmdline_txt="${ROOTFS_DIR}/cmdline.txt"

  if [[ ! -f "${cmdline_txt}" ]]; then
    return
  fi

  if ! grep -q 'brcmfmac.feature_disable=0x82000' "${cmdline_txt}"; then
    sed -i '1 s#$# brcmfmac.feature_disable=0x82000#' "${cmdline_txt}"
  fi
}

write_boot_partition() {
  local boot_image="$1"

  rm -rf "${ROOTFS_DIR}"
  mkdir -p "${ROOTFS_DIR}"

  mcopy -s -i "${boot_image}" ::* "${ROOTFS_DIR}/"

  patch_config_txt
  patch_cmdline_txt

  mcopy -s -o -i "${boot_image}" "${ROOTFS_DIR}"/* ::
}

write_file_to_ext_image() {
  local ext_image="$1"
  local source_path="$2"
  local target_path="$3"

  debugfs -w -R "mkdir $(dirname "${target_path}")" "${ext_image}" >/dev/null 2>&1 || true
  debugfs -w -R "rm ${target_path}" "${ext_image}" >/dev/null 2>&1 || true
  debugfs -w -R "write ${source_path} ${target_path}" "${ext_image}" >/dev/null
}

prepare_runtime_tree() {
  local runtime_dir="${STAGING_DIR}/runtime"
  local mydata_root="${STAGING_DIR}/mydata-root"

  rm -rf "${runtime_dir}" "${mydata_root}"
  mkdir -p "${runtime_dir}/home/tc/demo" \
           "${mydata_root}/opt" \
           "${mydata_root}/etc" \
           "${mydata_root}/home/tc"

  cp "${BIN_DIR}/first" "${runtime_dir}/home/tc/demo/first"
  cp "${BIN_DIR}/second" "${runtime_dir}/home/tc/demo/second"
  cp "${BIN_DIR}/third" "${runtime_dir}/home/tc/demo/third"

  cat > "${runtime_dir}/home/tc/demo/demo-menu.sh" <<'EOF'
#!/bin/sh

while true; do
  printf '\n=== TinyCore Raspberry Pi Demo Launcher ===\n'
  printf '1. first.c audio demo\n'
  printf '2. second.c audio + ALSA device list demo\n'
  printf '3. third.c audio + UTF-8 menu demo\n'
  printf '4. Reboot\n'
  printf '5. Power off\n'
  printf 'Select an option: '
  IFS= read -r choice

  case "${choice}" in
    1) /home/tc/demo/first ;;
    2) /home/tc/demo/second ;;
    3) /home/tc/demo/third ;;
    4) reboot ;;
    5) poweroff ;;
    *) printf 'Invalid choice.\n' ;;
  esac
done
EOF

  cat > "${runtime_dir}/home/tc/demo/launch-on-tty1.sh" <<'EOF'
#!/bin/sh

TTY_DEVICE=/dev/tty1

while true; do
  if [ -c "${TTY_DEVICE}" ]; then
    clear > "${TTY_DEVICE}" 2>/dev/null || true
    /home/tc/demo/demo-menu.sh < "${TTY_DEVICE}" > "${TTY_DEVICE}" 2>&1
    printf '\nDemo menu exited. Restarting in 2 seconds...\n' > "${TTY_DEVICE}"
  fi
  sleep 2
done
EOF

  chmod +x "${runtime_dir}/home/tc/demo/"*.sh

  cat > "${mydata_root}/opt/bootlocal.sh" <<EOF
#!/bin/sh

if command -v alsactl >/dev/null 2>&1; then
  alsactl init >/tmp/alsactl-init.log 2>&1 || true
  [ -f /usr/local/etc/alsa/asound.state ] && alsactl restore >/tmp/alsactl-restore.log 2>&1 || true
fi

if [ -s /etc/wpa_supplicant.conf ] && grep -q '^    ssid="' /etc/wpa_supplicant.conf; then
  wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -Dnl80211,wext >/tmp/wpa_supplicant.log 2>&1 || true
  udhcpc -b -i wlan0 >/tmp/udhcpc.log 2>&1 || true
fi

/home/tc/demo/launch-on-tty1.sh >/tmp/demo-launcher.log 2>&1 &
EOF

  cat > "${mydata_root}/opt/.filetool.lst" <<'EOF'
opt
home/tc
etc/wpa_supplicant.conf
etc/asound.conf
usr/local/etc/alsa/asound.state
var/lib/alsa
EOF

  cat > "${mydata_root}/etc/asound.conf" <<EOF
pcm.!default {
    type plug
    slave.pcm "hw:${ALSA_CARD},${ALSA_DEVICE}"
}

ctl.!default {
    type hw
    card ${ALSA_CARD}
}
EOF

  if [[ -n "${WIFI_SSID}" && -n "${WIFI_PSK}" ]]; then
    cat > "${mydata_root}/etc/wpa_supplicant.conf" <<EOF
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
    : > "${mydata_root}/etc/wpa_supplicant.conf"
  fi

  chmod +x "${mydata_root}/opt/bootlocal.sh"

  tar -C "${mydata_root}" -czf "${STAGING_DIR}/mydata.tgz" .
  printf '%s\t%s' "${runtime_dir}" "${STAGING_DIR}/mydata.tgz"
}

populate_root_partition() {
  local root_image="$1"
  local runtime_dir="$2"
  local mydata_tgz="$3"
  local optional_dir="/tce/optional"
  local runtime_target="/home/tc/demo"

  debugfs -w -R "mkdir /tce" "${root_image}" >/dev/null 2>&1 || true
  debugfs -w -R "mkdir ${optional_dir}" "${root_image}" >/dev/null 2>&1 || true
  debugfs -w -R "mkdir /home" "${root_image}" >/dev/null 2>&1 || true
  debugfs -w -R "mkdir /home/tc" "${root_image}" >/dev/null 2>&1 || true
  debugfs -w -R "mkdir ${runtime_target}" "${root_image}" >/dev/null 2>&1 || true

  local onboot_file="${STAGING_DIR}/onboot.lst"
  : > "${onboot_file}"

  while IFS= read -r file_path; do
    local base_name
    base_name="$(basename "${file_path}")"
    write_file_to_ext_image "${root_image}" "${file_path}" "${optional_dir}/${base_name}"
    if [[ "${base_name}" == *.tcz ]]; then
      printf '%s\n' "${base_name}" >> "${onboot_file}"
    fi
  done < <(find "${EXT_DIR}" -maxdepth 1 -type f | sort)

  while IFS= read -r runtime_file; do
    local relative_path="${runtime_file#${runtime_dir}}"
    write_file_to_ext_image "${root_image}" "${runtime_file}" "${relative_path}"
  done < <(find "${runtime_dir}" -type f | sort)

  write_file_to_ext_image "${root_image}" "${mydata_tgz}" "/tce/mydata.tgz"
  write_file_to_ext_image "${root_image}" "${onboot_file}" "/tce/onboot.lst"
}

repack_image() {
  local img_path="$1"
  local boot_image="$2"
  local root_image="$3"

  while IFS=$'\t' read -r node start _size _type; do
    case "${node}" in
      *1)
        dd if="${boot_image}" of="${img_path}" bs=512 seek="${start}" conv=notrunc status=none
        ;;
      *2)
        dd if="${root_image}" of="${img_path}" bs=512 seek="${start}" conv=notrunc status=none
        ;;
    esac
  done < <(get_partition_meta "${img_path}")
}

main() {
  local release_asset
  release_asset="$(select_release_asset)"

  log "Selected base image asset: ${release_asset}"

  local alsa_modules
  local wireless_modules
  alsa_modules="$(select_kernel_extension 'alsa-modules')"
  wireless_modules="$(select_kernel_extension 'wireless')"

  log "Selected ALSA kernel extension: ${alsa_modules}"
  log "Selected WiFi kernel extension: ${wireless_modules}"

  compile_programs

  download_extension_recursive "alsa.tcz"
  download_extension_recursive "alsa-utils.tcz"
  download_extension_recursive "alsa-plugins.tcz"
  download_extension_recursive "${alsa_modules}"
  download_extension_recursive "firmware-rpi-wifi.tcz"
  download_extension_recursive "wireless_tools.tcz"
  download_extension_recursive "wpa_supplicant.tcz"
  download_extension_recursive "wifi.tcz"
  download_extension_recursive "regdb.tcz"
  download_extension_recursive "${wireless_modules}"

  local image_path
  image_path="$(extract_image "${release_asset}")"

  local boot_image root_image
  IFS=$'\t' read -r boot_image root_image <<< "$(extract_partitions "${image_path}")"

  write_boot_partition "${boot_image}"

  local runtime_dir mydata_tgz
  IFS=$'\t' read -r runtime_dir mydata_tgz <<< "$(prepare_runtime_tree)"
  populate_root_partition "${root_image}" "${runtime_dir}" "${mydata_tgz}"

  repack_image "${image_path}" "${boot_image}" "${root_image}"

  local output_image="${OUTPUT_DIR}/custom-picore-rpi3-${TARGET_ARCH}.img"
  cp "${image_path}" "${output_image}"
  gzip -f -c "${output_image}" > "${output_image}.gz"

  cat > "${OUTPUT_DIR}/build-summary.txt" <<EOF
Build complete.
Architecture: ${TARGET_ARCH} (${TARGET_BITS}-bit)
TinyCore channel: ${TINYCORE_MAJOR}
Base image: ${release_asset}
ALSA card/device: ${ALSA_CARD},${ALSA_DEVICE}
HDMI audio forced: ${ENABLE_HDMI_AUDIO}
WiFi SSID configured: $( [[ -n "${WIFI_SSID}" ]] && printf 'yes' || printf 'no' )
Flashable image: ${output_image}
Compressed image: ${output_image}.gz
EOF

  log "Custom image written to ${output_image}"
  log "Compressed image written to ${output_image}.gz"
}

main "$@"
