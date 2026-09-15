#!/bin/bash
# Installer untuk ESP32-S3 Smart Display

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

echo "=== Done! ==="
echo "How to use: ./venv/bin/python monitor.py"