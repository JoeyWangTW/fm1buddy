#!/bin/sh
# Make a Raspberry Pi the fm1buddy USB MIDI host box: every USB MIDI device plugged into it is connected
# to every other one, both ways, and stays so as things come and go. Run once, as root:
#   curl -fsSL https://raw.githubusercontent.com/konsumer/fm1buddy/main/usbhost/pi-setup.sh | sudo sh
set -e
apt-get install -y -qq alsa-utils >/dev/null

cat >/usr/local/bin/fm1buddy-midi <<'EOF'
#!/bin/sh
# every USB MIDI port to every other one's, checked every 2 seconds
while true; do
  ports=$(aconnect -l | awk '/^client / { c = $2 + 0; hw = $0 ~ /card=/ } /^ +[0-9]+ / && hw { print c ":" $1 }')
  for a in $ports; do
    for b in $ports; do
      [ "${a%%:*}" = "${b%%:*}" ] || aconnect "$a" "$b" 2>/dev/null || true
    done
  done
  sleep 2
done
EOF
chmod +x /usr/local/bin/fm1buddy-midi

cat >/etc/systemd/system/fm1buddy-midi.service <<'EOF'
[Unit]
Description=fm1buddy: connect every USB MIDI device to every other one
After=sound.target

[Service]
ExecStart=/usr/local/bin/fm1buddy-midi
Restart=always

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable --now fm1buddy-midi
echo "Done: plug in the FM-1 and your controllers."
