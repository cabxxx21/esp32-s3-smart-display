
# ESP32-S3 Smart Desk Display

**Spotify • System Monitor • Hyprland Logs**

A custom **3.5-inch USB secondary display** powered by an **ESP32-S3** and **ILI9488 TFT**. The display acts as a real-time desktop companion, showing Spotify playback, Linux system statistics, ESP32 diagnostics, and Hyprland activity.

> Built with **ESP-IDF (C++)** on the microcontroller and **Python** on the host PC.

---

## ✨ Features

### 🎵 Page 1 · Spotify Monitor
- Album artwork
- Song title & artist
- Playback progress bar
- Time-synchronized lyrics (LRCLIB)

### 📊 Page 2 · System Monitor
- CPU usage
- RAM usage
- Disk usage
- CPU & GPU temperatures
- Color-coded temperature warnings

### ⚙️ Page 3 · ESP32 Status
- Free heap memory
- PSRAM usage
- FreeRTOS task monitoring
- Core 0 vs Core 1 workload

### 🖥️ Page 4 · System & Hyprland Log
- Filtered `journalctl` logs
- Real-time Hyprland events
- Workspace switching
- Application open/close events
- Local & Public IP information

---

## 🛠 Hardware Requirements

| Component | Specification |
|---|---|
| MCU | ESP32-S3 DevKit (8MB PSRAM recommended) |
| Display | 3.5" ILI9488 TFT (480×320, SPI, Non-Touch) |
| Connection | USB + Dupont jumper wires |

---

## 📌 Wiring (ESP32-S3 ↔ ILI9488)

> Keep SPI wires as short as possible for stable **40 MHz** communication.

| ILI9488 | ESP32-S3 |
|---|---|
| VCC / LED | 3.3V / 5V |
| GND | GND |
| CS | GPIO10 |
| DC | GPIO9 |
| RST | GPIO14 |
| MOSI | GPIO11 |
| SCK | GPIO12 |
| MISO | GPIO13 |

---

## 💻 Host PC Setup

**Tested on:** Arch Linux + Hyprland

### 1. Install dependencies

```bash
sudo pacman -S python python-pip git playerctl
pip install psutil
```

Or install everything through `requirements.txt`.

### 2. Clone the repository

```bash
git clone https://github.com/YOUR_USERNAME/esp32-s3-smart-display.git

cd esp32-s3-smart-display

python -m venv venv
source venv/bin/activate

pip install -r requirements.txt
```

---

## 🚀 Getting Started

### Flash the ESP32

1. Open the project in **VS Code**
2. Install the **ESP-IDF Extension**
3. Select target: `esp32s3`
4. **Build → Flash → Monitor**

### Run the host application

```bash
python monitor.py
```

The ESP32 communicates with the PC over **USB CDC Serial**.

---

## ⌨️ Controls

| Key | Function |
|---|---|
| **1** | Spotify Monitor |
| **2** | System Monitor |
| **3** | ESP32 Status |
| **4** | System & Hyprland Log |
| **Q** | Quit |

---

## ⚙️ Architecture

### ESP32 (ESP-IDF v6)

- **Language:** C++
- **Graphics:** LovyanGFX
- Double buffering using PSRAM sprites
- 100% flicker-free rendering
- Dual-core FreeRTOS architecture

| Task | Core | Purpose |
|---|---|---|
| `serial_task` | Core 0 | Receive & parse USB data |
| `ui_task` | Core 1 | Render LCD interface |

### Host PC (Python)

Responsible for collecting desktop information and sending it to the ESP32.

**Data sources**

- `psutil` → CPU, RAM, Disk
- `playerctl` → Spotify metadata
- `lrclib.net` → Synced lyrics
- `Hyprland socket2.sock` → Window events
- `journalctl` → System logs

**Serial protocol**

```text
MUS:  Spotify metadata
SYS:  System statistics
LOG:  Journal & Hyprland logs
LYR:  Synced lyrics
IMG:  Album artwork
```

Album artwork is resized on the PC and transmitted in **1024-byte** serial chunks.

---

## 📷 Preview

Add screenshots here.

```text
/docs/page1_spotify.png
/docs/page2_system.png
/docs/page3_status.png
/docs/page4_logs.png
```

---

## 📄 License

MIT License
