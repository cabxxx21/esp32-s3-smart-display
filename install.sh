#!/bin/bash
# Installer for ESP32-S3 Smart Display

# Automatically get the project directory path
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

echo "=== Installing dependencies ==="
if command -v pacman &> /dev/null; then
    sudo pacman -S --needed python-pip python-pillow playerctl
elif command -v apt &> /dev/null; then
    sudo apt update
    sudo apt install python3-pip python3-pil playerctl
fi

echo "=== Setting up Python environment ==="
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
deactivate

echo "=== Creating Desktop Entry ==="
# Create .desktop file to be recognized by Rofi/App Launcher
DESKTOP_FILE="$HOME/.local/share/applications/esp-monitor.desktop"
cat <<EOF > "$DESKTOP_FILE"
[Desktop Entry]
Name=ESP32 Smart Display
Comment=Run ESP32 TFT Monitor
Exec=bash -c 'cd "$REPO_DIR" && ./venv/bin/python monitor.py; exec bash'
Icon=utilities-terminal
Terminal=true
Type=Application
Categories=Utility;
EOF
chmod +x "$DESKTOP_FILE"

echo "=== Done! ==="
echo "You can now launch 'ESP32 Smart Display' directly from Rofi / App Launcher!"
