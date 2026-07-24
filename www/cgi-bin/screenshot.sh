#!/bin/sh
echo "Content-Type: application/octet-stream"
echo ""
cat /dev/fb0 2>/dev/null
