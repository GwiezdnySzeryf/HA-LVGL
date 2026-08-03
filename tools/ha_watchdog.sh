#!/bin/sh
# MOES TPP01-Z | Persistent Background Watchdog
# Authored by OpenCode

PIDFILE="/tmp/ha_watchdog.pid"
if [ -f "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE" 2>/dev/null)
    if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
        exit 0
    fi
fi
echo $$ > "$PIDFILE"

CONFIG_FILE="/tuya/data/ha_config.json"

parse_json_bool() {
    FILE="$1"
    KEY="$2"
    DEFAULT="$3"
    if [ ! -f "$FILE" ]; then
        echo "$DEFAULT"
        return
    fi
    VAL=$(grep -o "\"$KEY\"[[:space:]]*:[[:space:]]*[a-z]*" "$FILE" | head -n 1 | cut -d: -f2 | tr -d ' "')
    if [ "$VAL" = "true" ]; then
        echo "1"
    elif [ "$VAL" = "false" ]; then
        echo "0"
    else
        echo "$DEFAULT"
    fi
}

while true; do
    # 0. Always prevent Tuya safe_mode triggers
    rm -f /tmp/safe_mode_triggered /tmp/safe_mode_record 2>/dev/null

    # 1. Maintain HTTP server (httpd) for WWW Portal
    WEB_AUTOSTART=$(parse_json_bool "$CONFIG_FILE" "web_autostart" "1")
    if [ "$WEB_AUTOSTART" = "1" ]; then
        chmod +x /tuya/data/www/cgi-bin/*.sh 2>/dev/null
        if ! iptables -C INPUT -p tcp --dport 80 -j ACCEPT 2>/dev/null; then
            iptables -I INPUT -p tcp --dport 80 -j ACCEPT 2>/dev/null
        fi

        if ! pidof httpd >/dev/null 2>&1; then
            httpd -h /tuya/data/www -p 80 &
            echo "[Watchdog] Restarted httpd web server." >> /tmp/ha_watchdog.log
        fi
    fi

    # 2. Maintain ADB daemon for fallback debugging
    if ! pidof adbd >/dev/null 2>&1; then
        /etc/init.d/S50adbd start >/dev/null 2>&1 &
    fi

    # 3. Maintain Wi-Fi wpa_supplicant, DHCP daemon & default route
    if ! pidof wpa_supplicant >/dev/null 2>&1; then
        if [ -f "/tuya/data/wpa_0_8.conf" ]; then
            wpa_supplicant -D nl80211 -i wlan0 -c /tuya/data/wpa_0_8.conf -B 2>/dev/null
        else
            wpa_supplicant -B -i wlan0 -c /etc/wpa_supplicant.conf 2>/dev/null
        fi
    fi
    if ! pidof udhcpc >/dev/null 2>&1; then
        udhcpc -i wlan0 -b -p /var/run/udhcpc.wlan0.pid 2>/dev/null &
    fi
    if ! ip route show dev wlan0 2>/dev/null | grep -q default; then
        ip route add default via 192.168.1.1 dev wlan0 2>/dev/null
    fi

    # 4. Handle Screen Application (HA Panel vs Tuya GUI)
    HA_AUTOSTART=$(parse_json_bool "$CONFIG_FILE" "ha_autostart" "1")
    FACTORY_ACTIVE=0
    if pidof voice_control_factory >/dev/null 2>&1 || [ -f "/tmp/product_test_flag" ]; then
        FACTORY_ACTIVE=1
    fi

    if [ "$FACTORY_ACTIVE" = "0" ]; then
        if pidof ha_panel >/dev/null 2>&1; then
            # HA Panel is active: pause Tuya monitor so it does not restart voice_control
            killall -STOP tuya_monitor.sh 2>/dev/null
            if pidof voice_control >/dev/null 2>&1 || pidof voice_control_safe_mode >/dev/null 2>&1; then
                killall -9 voice_control voice_control_safe_mode 2>/dev/null
            fi
        elif [ "$HA_AUTOSTART" = "1" ]; then
            # HA Panel autostart is enabled: pause Tuya monitor, kill Tuya GUI, launch HA Panel
            killall -STOP tuya_monitor.sh 2>/dev/null
            killall -9 voice_control voice_control_safe_mode 2>/dev/null
            if [ -x "/tuya/data/ha_panel" ]; then
                nohup /tuya/data/ha_panel >/tmp/ha_panel.log 2>&1 &
                echo "[Watchdog] Started ha_panel app." >> /tmp/ha_watchdog.log
            fi
        fi
    fi

    sleep 3
done
