#!/bin/sh

MANIFEST_FILE=/usr/local/share/demo-examples/examples.manifest

while true; do
    printf '\n=== TinyCore Raspberry Pi Demo Launcher ===\n'

    count=0
    while IFS= read -r example_name; do
        [ -n "$example_name" ] || continue
        count=$((count + 1))
        eval "example_${count}='${example_name}'"
        printf '%d. Run %s\n' "$count" "$example_name"
    done < "$MANIFEST_FILE"

    reboot_choice=$((count + 1))
    poweroff_choice=$((count + 2))

    printf '%d. Reboot\n' "$reboot_choice"
    printf '%d. Power off\n' "$poweroff_choice"
    printf 'Select an option: '
    IFS= read -r choice

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
    if [ -n "$selected_example" ] && [ -x "/usr/local/bin/${selected_example}" ]; then
        "/usr/local/bin/${selected_example}"
    else
        printf 'Invalid choice.\n'
    fi
done
