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

MQTT_ENABLED="false"
MQTT_HOST=""
MQTT_PORT="1883"
MQTT_USER=""
MQTT_PASS_SET="false"
MQTT_TOPIC="panel/tpp01"
MQTT_DISCOVERY="true"

if [ -f "/tuya/data/ha_config.json" ]; then
    HA_CONFIGURED="true"
    HA_URL=$(grep -o '"ha_url": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    RAW_TOKEN=$(grep -o '"ha_token": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    if [ -n "$RAW_TOKEN" ]; then HA_TOKEN_SET="true"; fi
    ENTITY_1=$(grep -o '"entity_1": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    ENTITY_1_NAME=$(grep -o '"entity_1_name": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    ENTITY_2=$(grep -o '"entity_2": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    ENTITY_2_NAME=$(grep -o '"entity_2_name": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)

    RAW_MQTT_EN=$(grep -o '"mqtt_enabled": *[a-z0-9]*' /tuya/data/ha_config.json | head -n1 | awk '{print $2}')
    if [ "$RAW_MQTT_EN" = "true" ] || [ "$RAW_MQTT_EN" = "1" ]; then MQTT_ENABLED="true"; fi
    
    MQTT_HOST=$(grep -o '"mqtt_host": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    
    RAW_MQTT_PORT=$(grep -o '"mqtt_port": *[0-9]*' /tuya/data/ha_config.json | head -n1 | awk '{print $2}')
    if [ -n "$RAW_MQTT_PORT" ]; then MQTT_PORT="$RAW_MQTT_PORT"; fi
    
    MQTT_USER=$(grep -o '"mqtt_user": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    RAW_MQTT_PASS=$(grep -o '"mqtt_pass": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    if [ -n "$RAW_MQTT_PASS" ]; then MQTT_PASS_SET="true"; fi

    RAW_MQTT_TOPIC=$(grep -o '"mqtt_topic": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    if [ -n "$RAW_MQTT_TOPIC" ]; then MQTT_TOPIC="$RAW_MQTT_TOPIC"; fi

    RAW_MQTT_DISC=$(grep -o '"mqtt_discovery": *[a-z0-9]*' /tuya/data/ha_config.json | head -n1 | awk '{print $2}')
    if [ "$RAW_MQTT_DISC" = "false" ] || [ "$RAW_MQTT_DISC" = "0" ]; then MQTT_DISCOVERY="false"; fi
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
  "mqtt_enabled": $MQTT_ENABLED,
  "mqtt_host": "$MQTT_HOST",
  "mqtt_port": $MQTT_PORT,
  "mqtt_user": "$MQTT_USER",
  "mqtt_pass_set": $MQTT_PASS_SET,
  "mqtt_topic": "$MQTT_TOPIC",
  "mqtt_discovery": $MQTT_DISCOVERY,
  "uptime": "$UPTIME"
}
EOF
