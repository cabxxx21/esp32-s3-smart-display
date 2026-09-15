#!/usr/bin/env python3
import time
import subprocess
import requests
import io
import re
import sys
import os
import socket
import threading
import psutil
from serial import Serial
from serial.tools import list_ports
from PIL import Image

current_page = 0
ser = None

def find_esp32():
    for port in list_ports.comports():
        if "303A" in port.hwid.upper() or "VID:PID=303A" in port.hwid.upper():
            return port.device
    for port in list_ports.comports():
        if "ttyACM" in port.device:
            return port.device
    return None

last_pub_ip_check = 0
pub_ip = "Loading..."

def get_network_info():
    global last_pub_ip_check, pub_ip
    local_ip = "No IP"
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(("8.8.8.8", 80))
        local_ip = s.getsockname()[0]
        s.close()
    except: pass
    
    current_time = time.time()
    if current_time - last_pub_ip_check > 600:
        try:
            r = requests.get("https://api.ipify.org?format=json", timeout=3)
            if r.status_code == 200: pub_ip = r.json().get("ip", "Unknown")
            last_pub_ip_check = current_time
        except: pub_ip = "Unknown"
    return local_ip, pub_ip

def get_hw_temps():
    cpu_temp, gpu_temp = "0", "0"
    try:
        temps = psutil.sensors_temperatures()
        for key in ["coretemp", "k10temp", "cpu_thermal", "acpitz"]:
            if key in temps:
                cpu_temp = str(int(temps[key][0].current))
                break
    except: pass
    
    try:
        out = subprocess.check_output(["nvidia-smi", "--query-gpu=temperature.gpu", "--format=csv,noheader,nounits"], text=True).strip()
        gpu_temp = out.split('\n')[0]
    except:
        try:
            temps = psutil.sensors_temperatures()
            for key in ["amdgpu", "nvidia"]:
                if key in temps: gpu_temp = str(int(temps[key][0].current))
        except: pass
    return cpu_temp, gpu_temp

def get_synced_lyrics(artist, title):
    try:
        url = f"https://lrclib.net/api/get?artist_name={artist}&track_name={title}"
        r = requests.get(url, timeout=5)
        if r.status_code == 200:
            data = r.json()
            synced = data.get("syncedLyrics")
            if synced:
                lines = []
                for line in synced.split("\n"):
                    match = re.match(r'\[(\d+):(\d+\.\d+)\](.*)', line)
                    if match:
                        mins, secs, text = int(match.group(1)), float(match.group(2)), match.group(3).strip()
                        lines.append({'time': mins * 60 + secs, 'text': text})
                return lines
            plain = data.get("plainLyrics", "Lirik tidak ditemukan.").split("\n")
            return [{'time': 0, 'text': l} for l in plain]
    except: pass
    return [{'time': 0, 'text': "Gagal mengambil lirik."}]

def get_album_art_bytes(url):
    try:
        r = requests.get(url, timeout=5)
        img = Image.open(io.BytesIO(r.content)).resize((138, 138)).convert('RGB')
        pixels = img.tobytes()
        byte_arr = bytearray()
        for i in range(0, len(pixels), 3):
            r, g, b = pixels[i], pixels[i+1], pixels[i+2]
            c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
            byte_arr.append((c >> 8) & 0xFF)
            byte_arr.append(c & 0xFF)
        return byte_arr
    except: return None

def wait_ok():
    start_time = time.time()
    while time.time() - start_time < 5:
        if ser.in_waiting > 0:
            if ser.readline().decode().strip() == "OK": return True
    return False

def journalctl_reader():
    try:
        proc = subprocess.Popen(["journalctl", "-f", "-o", "cat", "--no-pager"], stdout=subprocess.PIPE, text=True)
        for line in proc.stdout:
            lower_line = line.lower()
            blacklist = ["ufw", "networkmanager", "wpa_supplicant", "dhcpcd", "resolved", "warp", "masque", "tunnel", "cloudflared", "connectivity", "newneighbour", "destination:", "route-change", "upload_stats", "dns_proxy", "dns proxy", "networkinfochanged", "handle_update", "handle_command", "actor_", "dns_manager", "dns_recovery", "handle_network_info_changed", "trust anchors", "resolv.conf", "reloading network name resolution", "flushed all caches", "cloudflarewarp", "positive trust", "negative trust"]
            if any(bl in lower_line for bl in blacklist): continue
            
            log_type = "info"
            if "failed" in lower_line or "error" in lower_line: log_type = "err"
            elif "warn" in lower_line: log_type = "warn"
            elif "systemd" in lower_line: log_type = "sys"
            elif "kernel" in lower_line: log_type = "kernel"
            elif "pacman" in lower_line: log_type = "pacman"
            
            line = line.strip()[:120]
            if line.startswith('['): line = line.split('] ', 1)[-1]
            if current_page == 3:
                line_c = line.replace(":", " ").replace("\n", " ").replace("|", " ")
                ser.write(f"LOG:{log_type}|{line_c}\n".encode())
    except: pass

def hyprland_event_reader():
    xdg_runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    hypr_sig = os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", "")
    if not hypr_sig: return # Skip kalau bukan Hyprland
    
    socket_path = f"{xdg_runtime}/hypr/{hypr_sig}/.socket2.sock"
    while True:
        try:
            client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            client.connect(socket_path)
            client.settimeout(1.0)
            buffer = ""
            while True:
                try:
                    data = client.recv(4096).decode('utf-8')
                    if not data: break
                    buffer += data
                    while "\n" in buffer:
                        line, buffer = buffer.split("\n", 1)
                        line = line.strip()
                        if not line: continue
                        parts = line.split(">>")
                        event = parts[0].strip()
                        payload = parts[1].strip() if len(parts) > 1 else ""
                        log_text = ""
                        
                        if event == "workspace": log_text = f"Workspace -> {payload}"
                        elif event == "openwindow": log_text = f"Opened -> {payload.split(',')[2] if len(payload.split(',')) > 2 else 'unknown'}"
                        elif event == "closewindow": log_text = "Closed -> Window"
                        elif event == "activewindow": log_text = f"Focus -> {payload.split(',')[1] if len(payload.split(',')) > 1 else 'unknown'}"
                        
                        if log_text and current_page == 3:
                            log_text = log_text.replace(":", " ").replace("\n", " ").replace("|", " ")[:80]
                            ser.write(f"LOG:hypr|{log_text}\n".encode())
                except socket.timeout: continue
                except: break
            client.close()
        except: time.sleep(2)

def read_key():
    import tty, termios
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(sys.stdin.fileno())
        ch = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch

def keyboard_listener():
    global current_page
    while True:
        c = read_key()
        if c in ['\x03', 'q', 'Q']:
            print("\n[!] Keluar dari script...")
            os._exit(0)
        elif c in ['1', '2', '3', '4']:
            current_page = int(c) - 1
            ser.write(f"PAGE:{current_page}\n".encode())
            print(f">> Pindah ke Page {current_page}")

def main():
    global ser
    port = find_esp32()
    if not port:
        print("ESP32 tidak ditemukan! Pastikan dicolok via USB.")
        sys.exit(1)
        
    print(f"ESP32 ditemukan di {port}")
    ser = Serial(port, 115200, timeout=1)
    ser.timeout = 5

    last_title = ""
    lyrics_data = []

    threading.Thread(target=keyboard_listener, daemon=True).start()
    threading.Thread(target=journalctl_reader, daemon=True).start()
    threading.Thread(target=hyprland_event_reader, daemon=True).start()
    
    print("System ready! Tekan 1-4 buat ganti halaman. (q buat keluar)")

    while True:
        # Spotify data
        try:
            status = subprocess.check_output(["playerctl", "status"], text=True).strip().lower()
            title = subprocess.check_output(["playerctl", "metadata", "title"], text=True).strip()
            artist = subprocess.check_output(["playerctl", "metadata", "artist"], text=True).strip()
            art_url = subprocess.check_output(["playerctl", "metadata", "mpris:artUrl"], text=True).strip()
            
            dur_us = subprocess.check_output(["playerctl", "metadata", "mpris:length"], text=True).strip()
            dur_ms = int(int(dur_us) / 1000) if dur_us else 0
            pos_sec = float(subprocess.check_output(["playerctl", "position"], text=True).strip())
            pos_ms = int(pos_sec * 1000)
            
            if title != last_title:
                lyrics_data = get_synced_lyrics(artist, title)
                last_title = title
                time.sleep(0.5)
                if art_url:
                    img_bytes = get_album_art_bytes(art_url)
                    if img_bytes:
                        ser.write(b"IMG:\n"); ser.flush()
                        if wait_ok():
                            for i in range(0, len(img_bytes), 1024):
                                ser.write(img_bytes[i:i+1024]); ser.flush()
                                if not wait_ok(): break
                
            ser.write(f"MUS:{title[:40]}|{artist[:40]}|{status}\n".encode())
            ser.write(f"PRG:{pos_ms}|{dur_ms}\n".encode())
            
            current_idx = -1
            for i, l in enumerate(lyrics_data):
                if l['time'] <= pos_sec: current_idx = i
                else: break
                
            for i in range(3):
                idx = current_idx + i if current_idx >= 0 else i
                line_text = lyrics_data[idx]['text'] if idx >= 0 and idx < len(lyrics_data) else "..."
                ser.write(f"LYR:{i}|{line_text[:60]}\n".encode())
        except:
            ser.write(b"MUS:No Player|Idle|stopped\n")
            ser.write(b"PRG:0|0\n")
            for i in range(3): ser.write(f"LYR:{i}|...\n".encode())
        
        # System Monitor data
        if current_page == 1:
            try:
                cpu = psutil.cpu_percent(interval=None)
                ram = psutil.virtual_memory().percent
                disk = psutil.disk_usage('/').percent
                cpu_t, gpu_t = get_hw_temps()
                if not gpu_t or gpu_t == "0": gpu_t = "0"
                ser.write(f"SYS:{cpu:.0f}|{ram:.0f}|{disk:.0f}|{cpu_t}|{gpu_t}\n".encode())
            except: pass

        # Network Info data
        if current_page == 3:
            try:
                local_ip, pub_ip_val = get_network_info()
                ser.write(f"NET:{local_ip}|{pub_ip_val}\n".encode())
            except: pass
            
        time.sleep(1)

if __name__ == "__main__":
    main()