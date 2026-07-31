#!/bin/sh

json_response() {
    printf 'Content-Type: application/json; charset=utf-8\r\n'
    printf 'Cache-Control: no-store\r\n\r\n'
    printf '%s\n' "$1"
}

if [ "$REQUEST_METHOD" != "POST" ] || [ "$HTTP_X_REQUESTED_WITH" != "TPP01-Panel" ]; then
    printf 'Status: 403 Forbidden\r\n'
    json_response '{"ok":false,"error":"Invalid request origin"}'
    exit 1
fi

CMD=$(printf '%s' "$QUERY_STRING" | grep -o 'cmd=[^&]*' | cut -d= -f2)
VAL=$(printf '%s' "$QUERY_STRING" | grep -o 'val=[^&]*' | cut -d= -f2)

case "$CMD" in
    brightness)
        case "$VAL" in
            ''|*[!0-9]*)
                printf 'Status: 400 Bad Request\r\n'
                json_response '{"ok":false,"error":"Invalid brightness"}'
                exit 1
                ;;
        esac
        if [ "$VAL" -lt 5 ]; then VAL=5; fi
        if [ "$VAL" -gt 100 ]; then VAL=100; fi
        MAX_BRIGHTNESS=$(cat /sys/class/backlight/backlight/max_brightness 2>/dev/null)
        case "$MAX_BRIGHTNESS" in ''|*[!0-9]*) MAX_BRIGHTNESS=255 ;; esac
        RAW_BRIGHTNESS=$((VAL * MAX_BRIGHTNESS / 100))
        printf '0\n' > /tuya/data/ha_auto_brightness
        printf '%s\n' "$VAL" > /tuya/data/ha_brightness
        printf '%s\n' "$RAW_BRIGHTNESS" > /sys/class/backlight/backlight/brightness 2>/dev/null
        ;;
    start_ha|restart_app)
        (sleep 1; rm -f /tmp/safe_mode_triggered /tmp/safe_mode_record 2>/dev/null; killall -STOP tuya_monitor.sh 2>/dev/null; killall -9 voice_control voice_control_safe_mode ha_panel 2>/dev/null; nohup /tuya/data/ha_panel >/tmp/ha_panel.log 2>&1) </dev/null >/dev/null 2>&1 &
        ;;
    start_tuya)
        (sleep 1; rm -f /tmp/safe_mode_triggered /tmp/safe_mode_record 2>/dev/null; killall -9 ha_panel 2>/dev/null; killall -CONT tuya_monitor.sh 2>/dev/null; if ! pidof voice_control >/dev/null 2>&1; then nohup /tuya/app/tuya_monitor.sh >/dev/null 2>&1; fi) </dev/null >/dev/null 2>&1 &
        ;;
    reboot)
        (sleep 1; reboot) >/dev/null 2>&1 &
        ;;
    *)
        printf 'Status: 400 Bad Request\r\n'
        json_response '{"ok":false,"error":"Unknown command"}'
        exit 1
        ;;
esac

json_response "{\"ok\":true,\"command\":\"$CMD\"}"
