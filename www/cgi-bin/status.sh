#!/bin/sh
echo "Content-Type: application/json; charset=utf-8"
echo ""

IP=$(ifconfig wlan0 2>/dev/null | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1 }')
if [ -z "$IP" ]; then IP="127.0.0.1"; fi

HA_CONFIGURED="false"
HA_URL=""
if [ -f "/tuya/data/ha_config.json" ]; then
    HA_CONFIGURED="true"
    HA_URL=$(grep -o '"ha_url": *"[^"]*"' /tuya/data/ha_config.json | cut -d'"' -f4)
fi

UPTIME=$(uptime 2>/dev/null | sed 's/"/\\"/g')

cat <<EOF
{
  "ip": "$IP",
  "ha_configured": $HA_CONFIGURED,
  "ha_url": "$HA_URL",
  "uptime": "$UPTIME"
}
EOF
