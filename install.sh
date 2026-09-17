#!/usr/bin/env bash
set -Eeuo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
VENV_DIR="$REPO_DIR/venv"
DESKTOP_FILE="$HOME/.local/share/applications/esp-monitor.desktop"

trap 'echo ""; echo "Error: Installation failed at line $LINENO."; exit 1' ERR

echo "=== ESP32-S3 Smart Display Installer ==="

echo " -> Checking project files..."
if [[ ! -f "$REPO_DIR/monitor.py" ]] || [[ ! -f "$REPO_DIR/requirements.txt" ]]; then
    echo "Error: 'monitor.py' or 'requirements.txt' not found in $REPO_DIR"
    exit 1
fi

echo " -> Checking system dependencies..."
if command -v pacman &> /dev/null; then
    sudo pacman -S --needed python python-pip playerctl
elif command -v apt &> /dev/null; then
    sudo apt update
    sudo apt install -y python3 python3-pip python3-venv playerctl
else
    echo "Error: Unsupported package manager. Please install python3, pip, venv, and playerctl manually."
    exit 1
fi

echo " -> Checking Python environment..."
if command -v python3 &> /dev/null; then
    PYTHON_BIN="python3"
elif command -v python &> /dev/null; then
    PYTHON_BIN="python"
else
    echo "Error: Python is not installed or not found in PATH."
    exit 1
fi

echo " -> Setting up Python virtual environment..."
if [[ ! -f "$VENV_DIR/bin/activate" ]]; then
    "$PYTHON_BIN" -m venv "$VENV_DIR"
else
    echo "    Virtual environment already exists. Reusing."
fi

echo " -> Installing Python dependencies..."
"$VENV_DIR/bin/pip" install --upgrade pip
"$VENV_DIR/bin/pip" install -r "$REPO_DIR/requirements.txt"

echo " -> Creating desktop entry..."
mkdir -p "$(dirname "$DESKTOP_FILE")"
cat <<EOF > "$DESKTOP_FILE"
[Desktop Entry]
Name=ESP32 Smart Display
Comment=Run ESP32 TFT Monitor
Exec="$VENV_DIR/bin/python" "$REPO_DIR/monitor.py"
Path=$REPO_DIR
Icon=utilities-terminal
Terminal=true
Type=Application
Categories=Utility;
EOF
chmod +x "$DESKTOP_FILE"

if command -v apt &> /dev/null; then
    if ! groups | grep &> /dev/null "dialout"; then
        echo ""
        echo "=== ACTION REQUIRED: Serial Device Permission ==="
        echo "Your user is not in the 'dialout' group."
        echo "You might not be able to access the ESP32 serial port (/dev/ttyACM*)."
        echo "Run: sudo usermod -aG dialout \$USER"
        echo "Then, log out and log back in."
    fi
fi

echo ""
echo "=== Installation Complete! ==="
echo "Launch 'ESP32 Smart Display' from your App Launcher or run:"
echo "$VENV_DIR/bin/python $REPO_DIR/monitor.py"