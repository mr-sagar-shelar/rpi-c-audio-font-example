#!/bin/sh

TTY_DEVICE=/dev/tty1
DEMO_ROOT="${DEMO_ROOT:-/home/tc/demo-runtime}"
MENU_SCRIPT="${MENU_SCRIPT:-$DEMO_ROOT/demo-menu.sh}"
EXIT_SENTINEL=/tmp/demo-launcher.disabled

while true; do
    if [ -c "$TTY_DEVICE" ]; then
        clear > "$TTY_DEVICE" 2>/dev/null || true
        "$MENU_SCRIPT" < "$TTY_DEVICE" > "$TTY_DEVICE" 2>&1
        if [ -f "$EXIT_SENTINEL" ]; then
            rm -f "$EXIT_SENTINEL"
            printf '\nDemo launcher stopped. TinyCore shell remains available on tty1.\n' > "$TTY_DEVICE"
            break
        fi
        printf '\nLauncher exited unexpectedly. Restarting...\n' > "$TTY_DEVICE"
    fi
    sleep 2
done
