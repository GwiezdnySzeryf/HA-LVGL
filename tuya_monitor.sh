#!/bin/sh
# TPP01-Z custom monitor script for Home Assistant dashboard
# Authored by OpenCode

# 1. Ensure ADB daemon and debug NVRAM flags
nvram unset adb.disable 2>/dev/null
nvram set adb.ctr.dbg 1 2>/dev/null
nvram set tuya.debug.mode 1 2>/dev/null
nvram set PROJECT_DEBUG 0 2>/dev/null
nvram set persist.ftm.mode 0 2>/dev/null
nvram set product.test.method 0 2>/dev/null
nvram set wifi_on 1 2>/dev/null
nvram commit 2>/dev/null

/etc/init.d/S50adbd start 2>/dev/null &

# 2. Clear Tuya safe mode trigger flags and unblock ports in iptables
rm -f /tmp/safe_mode_triggered /tmp/safe_mode_record 2>/dev/null
iptables -I INPUT -p tcp --dport 80 -j ACCEPT 2>/dev/null
iptables -I INPUT -p tcp --dport 23 -j ACCEPT 2>/dev/null

# 3. Launch persistent background watchdog
if [ -x "/tuya/data/ha_watchdog.sh" ]; then
    /tuya/data/ha_watchdog.sh &
fi

# 4. Launch default OEM Tuya supervisor
/tuya/app/bin/daemon_avs_client.bin &
/tuya/app/tuya_monitor.sh &

echo "[$(date)] Custom monitor initialized with default Tuya software, watchdog, and USB ADB." >> /userdata/custom_monitor.log
