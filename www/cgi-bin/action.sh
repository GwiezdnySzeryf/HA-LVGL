#!/bin/sh
echo "Content-Type: application/json; charset=utf-8"
echo ""

CMD=$(echo "$QUERY_STRING" | grep -o 'cmd=[^&]*' | cut -d= -f2)
VAL=$(echo "$QUERY_STRING" | grep -o 'val=[^&]*' | cut -d= -f2)

if [ "$CMD" = "brightness" ] && [ -n "$VAL" ]; then
    RAW_BRIGHTNESS=$((VAL * 255 / 100))
    echo $RAW_BRIGHTNESS > /sys/class/backlight/backlight/brightness 2>/dev/null
fi

if [ "$CMD" = "restart_app" ]; then
    (sleep 1; killall -9 ha_panel; /tuya/data/ha_panel >/tmp/ha_panel.log 2>&1 &) &
fi

if [ "$CMD" = "reboot" ]; then
    (sleep 1; reboot) &
fi

if [ "$CMD" = "disconnect_ha" ]; then
    rm -f /tuya/data/ha_config.json
    (sleep 1; killall -9 ha_panel; /tuya/data/ha_panel >/tmp/ha_panel.log 2>&1 &) &
fi

cat <<EOF
{
  "status": "ok",
  "cmd": "$CMD"
}
EOF
