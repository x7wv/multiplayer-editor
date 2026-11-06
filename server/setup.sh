#!/bin/bash
# Multiplayer Editor server installer (by x7wv)
# Run on a fresh Ubuntu server:
#   curl -fsSL https://raw.githubusercontent.com/x7wv/multiplayer-editor/main/server/setup.sh | sudo bash
set -e
export DEBIAN_FRONTEND=noninteractive
echo "== Installing Multiplayer Editor server (made by x7wv) =="
apt-get update -y
apt-get install -y git nodejs npm iptables-persistent

if [ -d /opt/multiplayer-editor/.git ]; then
  git -C /opt/multiplayer-editor pull --ff-only
else
  git clone --depth 1 https://github.com/x7wv/multiplayer-editor /opt/multiplayer-editor
fi
cd /opt/multiplayer-editor/server
npm ci --omit=dev

# Oracle's Ubuntu images block ports by default - open 8080
if ! iptables -C INPUT -p tcp --dport 8080 -j ACCEPT 2>/dev/null; then
  iptables -I INPUT 1 -p tcp --dport 8080 -j ACCEPT
fi
netfilter-persistent save || true

cat > /etc/systemd/system/multiplayer-editor.service <<UNIT
[Unit]
Description=Multiplayer Editor server (by x7wv)
After=network.target

[Service]
WorkingDirectory=/opt/multiplayer-editor/server
ExecStart=$(command -v node) server.js
Environment=PORT=8080
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
UNIT
systemctl daemon-reload
systemctl enable --now multiplayer-editor
systemctl restart multiplayer-editor
sleep 2

IP=$(curl -fsS https://api.ipify.org || echo "YOUR-SERVER-IP")
echo
echo "========================================================"
echo " Multiplayer Editor server is running!"
echo " Put this in the mod's Server URL setting:"
echo "     ws://$IP:8080"
echo "========================================================"
