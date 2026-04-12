#!/bin/sh

find_tcedir() {
    if [ -d /etc/sysconfig/tcedir ]; then
        printf '%s\n' /etc/sysconfig/tcedir
        return 0
    fi

    if [ -f /opt/.tce_dir ]; then
        cat /opt/.tce_dir
        return 0
    fi

    for candidate in /mnt/mmcblk0p1/tce /mnt/mmcblk0p2/tce; do
        if [ -d "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    return 1
}

if [ -x /opt/load-demo-extensions.sh ]; then
    /opt/load-demo-extensions.sh boot >/tmp/tce-load-onboot.log 2>&1 || true
fi

if command -v alsactl >/dev/null 2>&1; then
    alsactl init >/tmp/alsactl-init.log 2>&1 || true
    [ -f /usr/local/etc/alsa/asound.state ] && alsactl restore >/tmp/alsactl-restore.log 2>&1 || true
fi

if [ -s /etc/wpa_supplicant.conf ] && grep -q '^network={' /etc/wpa_supplicant.conf; then
    ifconfig wlan0 up >/tmp/wlan0-up.log 2>&1 || true
    wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -Dnl80211,wext >/tmp/wpa_supplicant.log 2>&1 || true
    udhcpc -b -i wlan0 >/tmp/udhcpc.log 2>&1 || true
fi

printf '%s\n' 'Demo launcher autostart disabled. Use /home/tc/demo-runtime/bin/<example-name> to run examples manually.' > /tmp/demo-launcher.log
