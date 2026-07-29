#!/bin/sh
echo "Content-Type: application/json; charset=utf-8"
echo ""

IP=$(ifconfig wlan0 2>/dev/null | grep 'inet addr:' | cut -d: -f2 | awk '{ print $1 }')
if [ -z "$IP" ]; then IP="127.0.0.1"; fi

HA_CONFIGURED="false"
HA_TOKEN_SET="false"
HA_URL=""
ENTITY_1=""
ENTITY_1_NAME=""
ENTITY_2=""
ENTITY_2_NAME=""

if [ -f "/tuya/data/ha_config.json" ]; then
    HA_CONFIGURED="true"
    HA_URL=$(grep -o '"ha_url": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    RAW_TOKEN=$(grep -o '"ha_token": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    if [ -n "$RAW_TOKEN" ]; then HA_TOKEN_SET="true"; fi
    ENTITY_1=$(grep -o '"entity_1": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    ENTITY_1_NAME=$(grep -o '"entity_1_name": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    ENTITY_2=$(grep -o '"entity_2": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    ENTITY_2_NAME=$(grep -o '"entity_2_name": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
fi

UPTIME=$(uptime 2>/dev/null | sed 's/"/\\"/g')

cat <<EOF
{
  "ip": "$IP",
  "ha_configured": $HA_CONFIGURED,
  "ha_token_set": $HA_TOKEN_SET,
  "ha_url": "$HA_URL",
  "entity_1": "$ENTITY_1",
  "entity_1_name": "$ENTITY_1_NAME",
  "entity_2": "$ENTITY_2",
  "entity_2_name": "$ENTITY_2_NAME",
  "uptime": "$UPTIME"
}
EOF
