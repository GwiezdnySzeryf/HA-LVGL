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

HA_URL=$(urldecode "$HA_URL_RAW")
HA_TOKEN=$(urldecode "$HA_TOKEN_RAW")
ENTITY_1=$(urldecode "$ENTITY_1_RAW")
ENTITY_1_NAME=$(urldecode "$ENTITY_1_NAME_RAW")
ENTITY_2=$(urldecode "$ENTITY_2_RAW")
ENTITY_2_NAME=$(urldecode "$ENTITY_2_NAME_RAW")

# Basic validation
if [ -z "$HA_URL" ] || [ -z "$HA_TOKEN" ]; then
    show_error_page "Błąd Konfiguracji" "Adres URL oraz Token są wymagane."
    exit 0
fi

case "$HA_URL" in
    http://*|https://*) ;;
    *) 
        show_error_page "Błędny Adres URL" "Adres URL Home Assistant musi rozpoczynać się od http:// lub https://"
        exit 0
        ;;
esac

# Save credentials as JSON config
cat <<EOF > /tuya/data/ha_config.json
{
  "ha_url": "$HA_URL",
  "ha_token": "$HA_TOKEN",
  "entity_1": "$ENTITY_1",
  "entity_1_name": "$ENTITY_1_NAME",
  "entity_2": "$ENTITY_2",
  "entity_2_name": "$ENTITY_2_NAME"
}
EOF

chmod 600 /tuya/data/ha_config.json 2>/dev/null

# Output success page
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
            max-width: 440px; 
            width: 100%;
            box-shadow: 0 4px 20px rgba(0,0,0,0.5);
            box-sizing: border-box;
        }
        h1 { color: #4caf50; font-size: 22px; margin-top: 0; }
        p { color: #b0b0b0; line-height: 1.6; font-size: 14px; }
        .countdown-box {
            background: #282b34;
            padding: 12px;
            border-radius: 8px;
            margin: 20px 0;
            font-weight: bold;
            color: #ff9800;
            font-size: 15px;
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
        <p>Dane logowania do Home Assistant zostały pomyślnie zapisane w pamięci trwałej panelu.</p>
        
        <div class="countdown-box">
            🔄 Panel załaduje pulpit w ciągu: <span id="cnt">3</span> s
        </div>

        <p>Ekran urządzenia automatycznie przełączy się na główny pulpit sterowania.</p>
        
        <a href="/" class="btn-home">⬅️ Powrót do Strony Główny</a>
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
