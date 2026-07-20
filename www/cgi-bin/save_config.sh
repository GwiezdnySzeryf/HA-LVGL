#!/bin/sh
# CGI Save Config script for TPP01-Z
# Authored by OpenCode

echo "Content-Type: text/html; charset=utf-8"
echo ""

# Read Content-Length bytes from stdin (POST body)
read -n "$CONTENT_LENGTH" POST_DATA

# URL decode helper
urldecode() {
    printf '%b\n' "$(echo "$1" | sed 's/+/ /g; s/%\([0-9a-fA-F][0-9a-fA-F]\)/\\x\1/g')"
}

# Parse parameters: ha_url=...&ha_token=...
HA_URL_RAW=$(echo "$POST_DATA" | grep -o 'ha_url=[^&]*' | cut -d= -f2)
HA_TOKEN_RAW=$(echo "$POST_DATA" | grep -o 'ha_token=[^&]*' | cut -d= -f2)

HA_URL=$(urldecode "$HA_URL_RAW")
HA_TOKEN=$(urldecode "$HA_TOKEN_RAW")

# Save credentials as JSON config
cat <<EOF > /tuya/data/ha_config.json
{
  "ha_url": "$HA_URL",
  "ha_token": "$HA_TOKEN"
}
EOF

# Output beautiful dark-themed success page with correct utf-8 charset
cat <<EOF
<!DOCTYPE html>
<html>
<head>
    <meta charset="utf-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>Zapisano pomyślnie!</title>
    <style>
        body { 
            background: #1a1a1a; 
            color: #e0e0e0; 
            font-family: sans-serif; 
            text-align: center; 
            padding-top: 50px; 
        }
        .card { 
            background: #2d2d2d; 
            padding: 30px; 
            border-radius: 12px; 
            display: inline-block; 
            max-width: 90%; 
            box-shadow: 0 4px 20px rgba(0,0,0,0.5);
        }
        h1 { color: #4caf50; }
        p { color: #b0b0b0; line-height: 1.5; }
    </style>
</head>
<body>
    <div class="card">
        <h1>Konfiguracja Zapisana!</h1>
        <p>Dane logowania do Home Assistant zostały pomyślnie zapisane w panelu.</p>
        <p>Aplikacja <strong>ha_panel</strong> automatycznie załaduje konfigurację i uruchomi dashboard na ekranie urządzenia.</p>
        <p>Możesz teraz bezpiecznie zamknąć to okno na telefonie.</p>
    </div>
</body>
</html>
EOF
