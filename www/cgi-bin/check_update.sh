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

RELEASE_FILE="/tmp/github_release_$$.json"
trap 'rm -f "$RELEASE_FILE"' EXIT INT TERM
timeout 15 wget -qO "$RELEASE_FILE" \
    --header='Accept: application/vnd.github+json' \
    --header='User-Agent: TPP01-HA-Panel' \
    'https://api.github.com/repos/GwiezdnySzeryf/HA-LVGL/releases/latest' 2>/dev/null
WGET_STATUS=$?

RELEASE_SIZE=$(wc -c < "$RELEASE_FILE" 2>/dev/null)
case "$RELEASE_SIZE" in ''|*[!0-9]*) RELEASE_SIZE=0 ;; esac
if [ "$WGET_STATUS" -ne 0 ] || [ "$RELEASE_SIZE" -eq 0 ] || [ "$RELEASE_SIZE" -gt 262144 ]; then
    printf 'Status: 502 Bad Gateway\r\n'
    json_response '{"ok":false,"error":"GitHub request failed"}'
    exit 1
fi

printf 'Content-Type: application/json; charset=utf-8\r\n'
printf 'Cache-Control: no-store\r\n\r\n'
cat "$RELEASE_FILE"
