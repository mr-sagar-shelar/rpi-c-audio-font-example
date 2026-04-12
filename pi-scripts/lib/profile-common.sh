#!/bin/sh

set -eu

find_boot_mount() {
    if [ -d /etc/sysconfig/tcedir ]; then
        tcedir="$(cd /etc/sysconfig/tcedir && pwd)"
        candidate="$(cd "$tcedir/.." && pwd)"
        if [ -f "$candidate/config.txt" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    fi

    if [ -f /opt/.tce_dir ]; then
        tcedir="$(cat /opt/.tce_dir)"
        candidate="$(cd "$tcedir/.." 2>/dev/null && pwd || true)"
        if [ -n "$candidate" ] && [ -f "$candidate/config.txt" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    fi

    for candidate in /mnt/mmcblk0p1 /mnt/mmcblk0 /mnt/sda1 /media/mmcblk0p1; do
        if [ -f "$candidate/config.txt" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

ensure_boot_files_writable() {
    boot_mount="$1"

    if [ -w "$boot_mount/config.txt" ] && [ -w "$boot_mount/cmdline.txt" ]; then
        return 0
    fi

    if [ "$(id -u)" -ne 0 ] && command -v sudo >/dev/null 2>&1; then
        exec sudo "$0" "$@"
    fi

    if [ "$(id -u)" -ne 0 ]; then
        printf 'Please rerun with sudo: sudo %s\n' "$0" >&2
        exit 1
    fi
}

append_unique_lines_from_file() {
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

append_cmdline_tokens_from_file() {
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

apply_profile_dir() {
    profile_dir="$1"
    profile_name="$2"
    boot_mount="$(find_boot_mount)" || {
        printf 'Unable to find the Raspberry Pi boot partition.\n' >&2
        exit 1
    }

    ensure_boot_files_writable "$boot_mount" "$@"

    config_file="$boot_mount/config.txt"
    cmdline_file="$boot_mount/cmdline.txt"

    append_unique_lines_from_file "$profile_dir/config.txt.append" "$config_file"
    append_cmdline_tokens_from_file "$profile_dir/cmdline.append" "$cmdline_file"

    printf 'Applied profile: %s\n' "$profile_name"
    printf 'Updated: %s\n' "$config_file"
    printf 'Updated: %s\n' "$cmdline_file"
    printf 'Reboot required for changes to take effect.\n'
}
