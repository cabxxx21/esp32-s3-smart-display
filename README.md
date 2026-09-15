ESP32-S3 Smart Desk Display (Spotify, System Monitor & Hyprland Logs)

A custom 3.5" secondary monitor connected to a PC via USB. It displays real-time system information, music player status (Spotify), and desktop activity logs (Hyprland) using an ESP32-S3 and an ILI9488 LCD. Built entirely with ESP-IDF (C++) and Python on the host side.
✨ Features

    Spotify Monitor (Page 1): Displays album art, song title, progress bar, and time-synced lyrics.
    System Monitor (Page 2): Displays real-time CPU, RAM, and Disk usage, complete with CPU and GPU temperatures (with color-coded warnings).
    ESP32 Status (Page 3): Monitors internal ESP32 free RAM and FreeRTOS task distribution between Core 0 and Core 1.
    System & Hyprland Log (Page 4): Displays filtered journalctl logs (free from network/VPN spam) combined with real-time Hyprland events (opening/closing apps, switching workspaces), along with Local & Public IP info.

🛠️ Hardware Requirements

    Microcontroller: ESP32-S3 (DevKitC or similar with at least 8MB Octal PSRAM).
    Display: 3.5" ILI9488 LCD (Non-Touch, 480x320, SPI).
    Cables: Dupont jumper wires (male-female).

📌 Wiring Diagram (ESP32-S3 ↔ ILI9488)

Keep SPI cables as short as possible to ensure a stable 40MHz signal.
Pin LCD ILI9488	Pin ESP32-S3	Description
VCC / LED	3.3V / 5V	Power / Backlight
GND	GND	Ground
CS	GPIO 10	Chip Select
DC / RS	GPIO 9	Data/Command
RST / RES	GPIO 14	Reset
SDI / MOSI	GPIO 11	SPI Master Out
SCK / CLK	GPIO 12	SPI Clock
SDO / MISO	GPIO 13	SPI Master In
💻 Software Setup (Host PC)

Designed to be Plug & Play on Linux systems (Tested on Arch Linux + Hyprland).
1. Install PC Dependencies

Ensure you have the following packages installed on your Linux system:

    python3, python-pip, git
    playerctl (for music control)
    psutil (for system monitoring)

2. Clone Repo & Setup Python Environment

git clone https://github.com/YOUR_USERNAME/esp32-s3-smart-display.gitcd esp32-s3-smart-displaypython3 -m venv venvsource venv/bin/activatepip install -r requirements.txt

🚀 Usage
Step 1: Flash ESP32 Firmware

    Open this project folder in VSCode (ensure the ESP-IDF extension is installed).
    Set the target board to esp32s3.
    Click the Build (🔧) icon, then Flash and Monitor (🔥).

Step 2: Run the Host Script

Once the ESP32 is powered on and connected via USB, run the Python script on your PC:

python monitor.py

Step 3: Navigation

While the script is running, press keys on your PC keyboard to switch pages on the LCD:

     Press 1 : Spotify Monitor
     Press 2 : System Monitor
     Press 3 : ESP32 Status
     Press 4 : System & Hyprland Log
     Press q : Quit the script

⚙️ Technical Details

     C++ (ESP-IDF v6): Uses the LovyanGFX library for UI rendering with Double Buffering (Sprite in PSRAM) to ensure a 100% flicker-free experience. Processes are split into two FreeRTOS tasks: serial_task on Core 0 (reading USB CDC) and ui_task on Core 1 (rendering UI).
     Python: Gathers data via psutil and subprocess (playerctl), fetches synced lyrics from lrclib.net API, and reads Hyprland socket events (socket2.sock) in real-time. Data is sent to the ESP32 via USB Serial using prefixed text protocols (MUS:, SYS:, LOG:, etc.). Album art is resized in Python and sent to the ESP32 in 1024-byte chunks.
