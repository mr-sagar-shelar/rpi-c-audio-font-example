#!/bin/sh

if command -v alsactl >/dev/null 2>&1; then
    alsactl init >/tmp/alsactl-init.log 2>&1 || true
    [ -f /usr/local/etc/alsa/asound.state ] && alsactl restore >/tmp/alsactl-restore.log 2>&1 || true
fi

if [ -s /etc/wpa_supplicant.conf ] && grep -q '^network={' /etc/wpa_supplicant.conf; then
    ifconfig wlan0 up >/tmp/wlan0-up.log 2>&1 || true
    wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf -Dnl80211,wext >/tmp/wpa_supplicant.log 2>&1 || true
    udhcpc -b -i wlan0 >/tmp/udhcpc.log 2>&1 || true
fi

/usr/local/bin/demo-launch-on-tty1.sh >/tmp/demo-launcher.log 2>&1 &
