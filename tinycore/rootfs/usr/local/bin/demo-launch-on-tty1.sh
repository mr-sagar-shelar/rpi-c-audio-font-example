#!/bin/sh

TTY_DEVICE=/dev/tty1

while true; do
    if [ -c "$TTY_DEVICE" ]; then
        clear > "$TTY_DEVICE" 2>/dev/null || true
        /usr/local/bin/demo-menu.sh < "$TTY_DEVICE" > "$TTY_DEVICE" 2>&1
        printf '\nLauncher exited. Restarting...\n' > "$TTY_DEVICE"
    fi
    sleep 2
done
