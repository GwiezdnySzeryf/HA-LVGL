#!/bin/sh

OUTPUT=$(/tuya/data/ha_panel --cgi-config)
STATUS=$?

printf '%s\n' "$OUTPUT"

if [ "$STATUS" -eq 0 ]; then
    (sleep 1; killall -9 ha_panel 2>/dev/null; /tuya/data/ha_panel >/tmp/ha_panel.log 2>&1 &) >/dev/null 2>&1 &
fi

exit "$STATUS"
