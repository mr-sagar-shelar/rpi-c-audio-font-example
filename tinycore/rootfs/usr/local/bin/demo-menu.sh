#!/bin/sh

DEMO_ROOT="${DEMO_ROOT:-/home/tc/demo-runtime}"
MANIFEST_FILE="${MANIFEST_FILE:-$DEMO_ROOT/examples.manifest}"
BINARY_DIR="${BINARY_DIR:-$DEMO_ROOT/bin}"
EXIT_SENTINEL=/tmp/demo-launcher.disabled

while true; do
    printf '\n=== TinyCore Raspberry Pi Demo Launcher ===\n'

    count=0
    while IFS= read -r example_name; do
        [ -n "$example_name" ] || continue
        count=$((count + 1))
        eval "example_${count}='${example_name}'"
        printf '%d. Run %s\n' "$count" "$example_name"
    done < "$MANIFEST_FILE"

    exit_choice=$((count + 1))
    reboot_choice=$((count + 2))
    poweroff_choice=$((count + 3))

    printf '%d. Exit To Shell\n' "$exit_choice"
    printf '%d. Reboot\n' "$reboot_choice"
    printf '%d. Power off\n' "$poweroff_choice"
    printf 'Select an option: '
    IFS= read -r choice

    if [ "$choice" = "$exit_choice" ]; then
        : > "$EXIT_SENTINEL"
        printf 'Exiting demo launcher. Returning to TinyCore shell.\n'
        exit 0
    fi

    if [ "$choice" = "$reboot_choice" ]; then
        reboot
        continue
    fi

    if [ "$choice" = "$poweroff_choice" ]; then
        poweroff
        continue
    fi

    case "$choice" in
        ''|*[!0-9]*)
            printf 'Invalid choice.\n'
            continue
            ;;
    esac

    eval "selected_example=\${example_${choice}:-}"
    if [ -n "$selected_example" ] && [ -x "${BINARY_DIR}/${selected_example}" ]; then
        "${BINARY_DIR}/${selected_example}"
    else
        printf 'Invalid choice.\n'
    fi
done
