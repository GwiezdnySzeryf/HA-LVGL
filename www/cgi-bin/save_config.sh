#!/bin/sh
# CGI Save Config script for TPP01-Z
# Authored by OpenCode

echo "Content-Type: text/html; charset=utf-8"
echo ""

# Read POST data safely across busybox ash and bash
if [ -n "$CONTENT_LENGTH" ] && [ "$CONTENT_LENGTH" -gt 0 ]; then
    POST_DATA=$(head -c "$CONTENT_LENGTH" 2>/dev/null)
else
    POST_DATA=$(cat)
fi

# URL decode helper
urldecode() {
    printf '%b\n' "$(echo "$1" | sed 's/+/ /g; s/%\([0-9a-fA-F][0-9a-fA-F]\)/\\x\1/g')"
}

# Helper to extract parameter value safely
get_param() {
    echo "$POST_DATA" | grep -o "$1=[^&]*" | head -n1 | sed "s/^$1=//"
}

show_error_page() {
    TITLE="$1"
    MESSAGE="$2"
    cat <<EOF
<!DOCTYPE html>
<html lang="pl">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>$TITLE</title>
    <style>
        body { 
            background: #121318; 
            color: #e0e0e0; 
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; 
            text-align: center; 
            padding: 40px 15px; 
            margin: 0;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 90vh;
        }
        .card { 
            background: #1d1f25; 
            padding: 35px 25px; 
            border-radius: 16px; 
            border: 1px solid #f44336;
            max-width: 440px; 
            width: 100%;
            box-shadow: 0 4px 20px rgba(0,0,0,0.5);
            box-sizing: border-box;
        }
        h1 { color: #f44336; font-size: 22px; margin-top: 0; }
        p { color: #b0b0b0; line-height: 1.6; font-size: 14px; }
        .btn-back {
            display: inline-block;
            margin-top: 15px;
            padding: 12px 24px;
            background-color: #3b3e4a;
            color: #ffffff;
            text-decoration: none;
            border-radius: 8px;
            font-weight: bold;
            font-size: 14px;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>❌ $TITLE</h1>
        <p>$MESSAGE</p>
        <a href="javascript:history.back()" class="btn-back">⬅️ Powrót do Formularza</a>
    </div>
</body>
</html>
EOF
}

HA_URL_RAW=$(get_param "ha_url")
HA_TOKEN_RAW=$(get_param "ha_token")
ENTITY_1_RAW=$(get_param "entity_1")
ENTITY_1_NAME_RAW=$(get_param "entity_1_name")
ENTITY_2_RAW=$(get_param "entity_2")
ENTITY_2_NAME_RAW=$(get_param "entity_2_name")

MQTT_ENABLED_RAW=$(get_param "mqtt_enabled")
MQTT_HOST_RAW=$(get_param "mqtt_host")
MQTT_PORT_RAW=$(get_param "mqtt_port")
MQTT_USER_RAW=$(get_param "mqtt_user")
MQTT_PASS_RAW=$(get_param "mqtt_pass")
MQTT_TOPIC_RAW=$(get_param "mqtt_topic")
MQTT_DISCOVERY_RAW=$(get_param "mqtt_discovery")

HA_URL=$(urldecode "$HA_URL_RAW")
HA_TOKEN=$(urldecode "$HA_TOKEN_RAW")
ENTITY_1=$(urldecode "$ENTITY_1_RAW")
ENTITY_1_NAME=$(urldecode "$ENTITY_1_NAME_RAW")
ENTITY_2=$(urldecode "$ENTITY_2_RAW")
ENTITY_2_NAME=$(urldecode "$ENTITY_2_NAME_RAW")

MQTT_ENABLED=$(urldecode "$MQTT_ENABLED_RAW")
MQTT_HOST=$(urldecode "$MQTT_HOST_RAW")
MQTT_PORT=$(urldecode "$MQTT_PORT_RAW")
MQTT_USER=$(urldecode "$MQTT_USER_RAW")
MQTT_PASS=$(urldecode "$MQTT_PASS_RAW")
MQTT_TOPIC=$(urldecode "$MQTT_TOPIC_RAW")
MQTT_DISCOVERY=$(urldecode "$MQTT_DISCOVERY_RAW")

# Read existing config values as defaults if not provided in POST
if [ -f "/tuya/data/ha_config.json" ]; then
    EXIST_URL=$(grep -o '"ha_url": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_TOKEN=$(grep -o '"ha_token": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_E1=$(grep -o '"entity_1": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_E1N=$(grep -o '"entity_1_name": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_E2=$(grep -o '"entity_2": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_E2N=$(grep -o '"entity_2_name": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)

    EXIST_M_EN=$(grep -o '"mqtt_enabled": *[a-z0-9]*' /tuya/data/ha_config.json | head -n1 | awk '{print $2}')
    EXIST_M_HOST=$(grep -o '"mqtt_host": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_M_PORT=$(grep -o '"mqtt_port": *[0-9]*' /tuya/data/ha_config.json | head -n1 | awk '{print $2}')
    EXIST_M_USER=$(grep -o '"mqtt_user": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_M_PASS=$(grep -o '"mqtt_pass": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_M_TOPIC=$(grep -o '"mqtt_topic": *"[^"]*"' /tuya/data/ha_config.json | head -n1 | cut -d'"' -f4)
    EXIST_M_DISC=$(grep -o '"mqtt_discovery": *[a-z0-9]*' /tuya/data/ha_config.json | head -n1 | awk '{print $2}')
fi

if [ -z "$HA_URL" ]; then HA_URL="$EXIST_URL"; fi

# If submitted token is empty or contains dots/masking, reuse existing token
case "$HA_TOKEN" in
    ""|*•*|*ZAMASKOWANY*) HA_TOKEN="$EXIST_TOKEN" ;;
esac

# MQTT Password masking/fallback
case "$MQTT_PASS" in
    ""|*•*|*ZAMASKOWANY*) MQTT_PASS="$EXIST_M_PASS" ;;
esac

if [ -z "$ENTITY_1" ]; then ENTITY_1="${EXIST_E1:-light.living_room}"; fi
if [ -z "$ENTITY_1_NAME" ]; then ENTITY_1_NAME="${EXIST_E1N:-ŚWIATŁO}"; fi
if [ -z "$ENTITY_2" ]; then ENTITY_2="${EXIST_E2:-switch.fan}"; fi
if [ -z "$ENTITY_2_NAME" ]; then ENTITY_2_NAME="${EXIST_E2N:-WENTYLATOR}"; fi

case "$MQTT_ENABLED" in
    "true"|"1"|"on"|"ON") MQTT_EN_BOOL="true" ;;
    "false"|"0"|"off"|"OFF") MQTT_EN_BOOL="false" ;;
    *)
        if [ -n "$EXIST_M_EN" ]; then MQTT_EN_BOOL="$EXIST_M_EN"; else MQTT_EN_BOOL="false"; fi
        ;;
esac

if [ -z "$MQTT_HOST" ]; then MQTT_HOST="$EXIST_M_HOST"; fi
if [ -z "$MQTT_PORT" ]; then MQTT_PORT="${EXIST_M_PORT:-1883}"; fi
if [ -z "$MQTT_USER" ]; then MQTT_USER="$EXIST_M_USER"; fi
if [ -z "$MQTT_TOPIC" ]; then MQTT_TOPIC="${EXIST_M_TOPIC:-panel/tpp01}"; fi

case "$MQTT_DISCOVERY" in
    "true"|"1"|"on"|"ON") MQTT_DISC_BOOL="true" ;;
    "false"|"0"|"off"|"OFF") MQTT_DISC_BOOL="false" ;;
    *)
        if [ -n "$EXIST_M_DISC" ]; then MQTT_DISC_BOOL="$EXIST_M_DISC"; else MQTT_DISC_BOOL="true"; fi
        ;;
esac

# Basic validation
if [ -z "$HA_URL" ] || [ -z "$HA_TOKEN" ]; then
    show_error_page "Błąd Konfiguracji" "Adres URL oraz Token są wymagane."
    exit 0
fi

# Auto-add http:// if missing
case "$HA_URL" in
    http://*|https://*) ;;
    *) HA_URL="http://$HA_URL" ;;
esac

# Save credentials as JSON config
cat <<EOF > /tuya/data/ha_config.json
{
  "ha_url": "$HA_URL",
  "ha_token": "$HA_TOKEN",
  "entity_1": "$ENTITY_1",
  "entity_1_name": "$ENTITY_1_NAME",
  "entity_2": "$ENTITY_2",
  "entity_2_name": "$ENTITY_2_NAME",
  "mqtt_enabled": $MQTT_EN_BOOL,
  "mqtt_host": "$MQTT_HOST",
  "mqtt_port": $MQTT_PORT,
  "mqtt_user": "$MQTT_USER",
  "mqtt_pass": "$MQTT_PASS",
  "mqtt_topic": "$MQTT_TOPIC",
  "mqtt_discovery": $MQTT_DISC_BOOL
}
EOF

chmod 600 /tuya/data/ha_config.json 2>/dev/null

# Automatically trigger background restart of ha_panel application
(sleep 1; killall -9 ha_panel; /tuya/data/ha_panel >/tmp/ha_panel.log 2>&1 &) &

# Output success page with real details
cat <<EOF
<!DOCTYPE html>
<html lang="pl">
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Zapisano pomyślnie!</title>
    <style>
        body { 
            background: #121318; 
            color: #e0e0e0; 
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; 
            text-align: center; 
            padding: 40px 15px; 
            margin: 0;
            display: flex;
            justify-content: center;
            align-items: center;
            min-height: 90vh;
        }
        .card { 
            background: #1d1f25; 
            padding: 35px 25px; 
            border-radius: 16px; 
            border: 1px solid #2a2d36;
            max-width: 460px; 
            width: 100%;
            box-shadow: 0 4px 20px rgba(0,0,0,0.5);
            box-sizing: border-box;
        }
        h1 { color: #4caf50; font-size: 22px; margin-top: 0; }
        p { color: #b0b0b0; line-height: 1.5; font-size: 14px; margin-bottom: 20px; }
        .info-list {
            background: #121318;
            border-radius: 12px;
            padding: 16px;
            border: 1px solid #2a2d36;
            text-align: left;
            margin-bottom: 20px;
        }
        .info-item {
            display: flex;
            justify-content: space-between;
            padding: 8px 0;
            border-bottom: 1px solid #20232b;
            font-size: 13px;
        }
        .info-item:last-child { border-bottom: none; }
        .info-item span { color: #a9a6b0; }
        .info-item strong { color: #ff9800; word-break: break-all; text-align: right; max-width: 60%; }
        .countdown-box {
            background: #282b34;
            padding: 12px;
            border-radius: 8px;
            margin: 15px 0;
            font-weight: bold;
            color: #03a9f4;
            font-size: 14px;
        }
        .btn-home {
            display: inline-block;
            margin-top: 15px;
            padding: 12px 24px;
            background-color: #1865a8;
            color: #ffffff;
            text-decoration: none;
            border-radius: 8px;
            font-weight: bold;
            font-size: 14px;
            transition: background-color 0.2s;
        }
        .btn-home:hover {
            background-color: #1e74c0;
        }
    </style>
</head>
<body>
    <div class="card">
        <h1>✅ Konfiguracja Zapisana!</h1>
        <p>Aplikacja panelu została zrestartowana i ładuje połączenie z Home Assistant.</p>
        
        <div class="info-list">
            <div class="info-item"><span>Serwer HA:</span> <strong>$HA_URL</strong></div>
            <div class="info-item"><span>Token:</span> <strong>•••••••••••••••• (Zapisano)</strong></div>
            <div class="info-item"><span>Przycisk 1:</span> <strong>$ENTITY_1_NAME ($ENTITY_1)</strong></div>
            <div class="info-item"><span>Przycisk 2:</span> <strong>$ENTITY_2_NAME ($ENTITY_2)</strong></div>
        </div>

        <div class="countdown-box">
            🔄 Restart aplikacji w toku. Ładowanie pulpitu za: <span id="cnt">3</span> s
        </div>
        
        <a href="/" class="btn-home">⬅️ Powrót do Strony Głównie</a>
    </div>

    <script>
        let timeLeft = 3;
        const cntEl = document.getElementById('cnt');
        const timer = setInterval(() => {
            timeLeft--;
            if (timeLeft <= 0) {
                clearInterval(timer);
                cntEl.innerText = '0 (Załadowano)';
            } else {
                cntEl.innerText = timeLeft;
            }
        }, 1000);
    </script>
</body>
</html>
EOF
