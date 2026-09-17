#!/usr/bin/env python3
import time
import subprocess
import requests
import io
import re
import sys
import os
import socket
import tty
import termios
import threading
import psutil
from serial import Serial
from serial.tools import list_ports
from serial.serialutil import SerialException
from PIL import Image

current_page = 0
max_pages = 3 # Default 3 (Debian/Non-Hyprland)

def find_esp32():
    for port in list_ports.comports():
        if "303A" in port.hwid.upper() or "VID:PID=303A" in port.hwid.upper():
            return port.device
    for port in list_ports.comports():
        if "ttyACM" in port.device:
            return port.device
    return None

class SafeSerial:
    def __init__(self):
        self.ser = None
        self.lock = threading.Lock()
        self._connect_loop()

    def _connect_loop(self):
        while True:
            port = find_esp32()
            if port:
                try:
                    self.ser = Serial(port, 115200, timeout=1)
                    self.ser.timeout = 5
                    print(f"\n[+] ESP32 connected at {port}")
                    return
                except SerialException:
                    pass
            print("[!] ESP32 not found or busy. Retrying in 3s...")
            time.sleep(3)

    def _reconnect(self):
        print("[!] ESP32 disconnected! Attempting to reconnect...")
        self.ser = None
        self._connect_loop()

    def write(self, data):
        with self.lock:
            if self.ser is None: return False
            try:
                self.ser.write(data)
                return True
            except SerialException:
                self._reconnect()
                return False

    def readline(self):
        with self.lock:
            if self.ser is None: return b""
            try:
                return self.ser.readline()
            except SerialException:
                self._reconnect()
                return b""

    def flush(self):
        with self.lock:
            if self.ser:
                try: self.ser.flush()
                except SerialException: self._reconnect()

    @property
    def in_waiting(self):
        if self.ser:
            try: return self.ser.in_waiting
            except SerialException: pass
        return 0

ser = None

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
            plain = data.get("plainLyrics", "Lyrics not found.").split("\n")
            return [{'time': 0, 'text': l} for l in plain]
    except: pass
    return [{'time': 0, 'text': "Failed to fetch lyrics."}]

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
            blacklist = ["ufw", "networkmanager", "wpa_supplicant", "dhcpcd", "resolved", "warp", "masque", "tunnel", "cloudflared", "connectivity", "newneighbour", "destination:", "route-change", "upload_stats", "dns_proxy", "dns proxy", "networkinfochanged", "handle_update", "handle_command", "actor_", "dns_manager", "dns_recovery", "handle_network_info_changed", "trust anchors", "resolv.conf", "reloading network name resolution", "flushed all caches", "cloudflarewarp", "positive trust", "negative trust", "queries", "periodic stats", "per origin", "mutilities"]
            if any(bl in lower_line for bl in blacklist): continue
            
            log_type = "info"
            if "segmentation fault" in lower_line or "core dump" in lower_line or "failed to launch" in lower_line or "traceback" in lower_line:
                log_type = "crit"
            elif "failed" in lower_line or "error" in lower_line:
                log_type = "err"
            elif "warn" in lower_line:
                log_type = "warn"
            elif "systemd" in lower_line:
                log_type = "sys"
            elif "kernel" in lower_line:
                log_type = "kernel"
            elif "pacman" in lower_line:
                log_type = "pacman"
            
            line = line.strip()[:120]
            if line.startswith('['): line = line.split('] ', 1)[-1]
            # Cuma kirim log kalau user ada di page 3 (index 3)
            if current_page == 3:
                line_c = line.replace(":", " ").replace("\n", " ").replace("|", " ")
                ser.write(f"LOG:{log_type}|{line_c}\n".encode())
    except: pass

def hyprland_event_reader():
    xdg_runtime = os.environ.get("XDG_RUNTIME_DIR", "/tmp")
    hypr_sig = os.environ.get("HYPRLAND_INSTANCE_SIGNATURE", "")
    if not hypr_sig: return # Kalau bukan Hyprland, thread ini langsung mati
    
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
                        
                        if event == "workspace":
                            log_text = f"[WS] Active Workspace -> {payload}"
                        elif event == "activewindow":
                            arr = payload.split(",")
                            app_class = arr[0] if len(arr) > 0 else "unknown"
                            app_title = arr[1] if len(arr) > 1 else "unknown"
                            log_text = f"[FOCUS] {app_class} -> {app_title}"
                        elif event == "openwindow":
                            arr = payload.split(",")
                            workspace = arr[1] if len(arr) > 1 else "unknown"
                            app_class = arr[2] if len(arr) > 2 else "unknown"
                            log_text = f"[LAUNCH] {app_class} on WS {workspace}"
                        elif event == "closewindow":
                            log_text = "[DESTROY] Window closed"
                        elif event == "fullscreen":
                            if payload == "1":
                                log_text = "[DISPLAY] Fullscreen -> ON"
                            else:
                                log_text = "[DISPLAY] Fullscreen -> OFF"
                        
                        if log_text and current_page == 3:
                            log_text = log_text.replace(":", " ").replace("\n", " ").replace("|", " ")[:80]
                            ser.write(f"LOG:hypr|{log_text}\n".encode())
                except socket.timeout:
                    continue
                except Exception:
                    break
            client.close()
        except Exception:
            time.sleep(2)

def read_key():
    fd = sys.stdin.fileno()
    old_settings = termios.tcgetattr(fd)
    try:
        tty.setraw(sys.stdin.fileno())
        ch = sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
    return ch

def keyboard_listener():
    global current_page, max_pages
    while True:
        c = read_key()
        if c in ['\x03', 'q', 'Q']:
            print("\n[!] Exiting script...")
            os._exit(0)
        elif c.isdigit() and 1 <= int(c) <= max_pages:
            current_page = int(c) - 1
            ser.write(f"PAGE:{current_page}\n".encode())
            print(f">> Switched to Page {current_page}")

def main():
    global ser, max_pages
    
    max_pages = 4
    
    is_hyprland = bool(os.environ.get("HYPRLAND_INSTANCE_SIGNATURE"))
    if is_hyprland:
        print("Hyprland detected. Enabling Hyprland event logs.")
    else:
        print("Non-Hyprland environment. Showing journalctl logs only on Page 4.")

    ser = SafeSerial()

    ser.write(f"MAXPAGES:{max_pages}\n".encode())
    time.sleep(0.5)

    last_title = ""
    lyrics_data = []

    threading.Thread(target=keyboard_listener, daemon=True).start()
    threading.Thread(target=journalctl_reader, daemon=True).start()
    if is_hyprland:
        threading.Thread(target=hyprland_event_reader, daemon=True).start()
    
    print(f"\nSystem ready! Press 1-{max_pages} to switch pages. (Press 'q' to quit)")

    while True:
        try:
            status = subprocess.check_output(["playerctl", "status"], text=True, stderr=subprocess.DEVNULL).strip().lower()
            title = subprocess.check_output(["playerctl", "metadata", "title"], text=True, stderr=subprocess.DEVNULL).strip()
            artist = subprocess.check_output(["playerctl", "metadata", "artist"], text=True, stderr=subprocess.DEVNULL).strip()
            art_url = subprocess.check_output(["playerctl", "metadata", "mpris:artUrl"], text=True, stderr=subprocess.DEVNULL).strip()
            
            dur_us = subprocess.check_output(["playerctl", "metadata", "mpris:length"], text=True, stderr=subprocess.DEVNULL).strip()
            dur_ms = int(int(dur_us) / 1000) if dur_us else 0
            pos_sec = float(subprocess.check_output(["playerctl", "position"], text=True, stderr=subprocess.DEVNULL).strip())
            pos_ms = int(pos_sec * 1000)
            
            if title != last_title:
                lyrics_data = get_synced_lyrics(artist, title)
                last_title = title
                time.sleep(0.5)
                if art_url:
                    img_bytes = get_album_art_bytes(art_url)
                    if img_bytes:
                        ser.write(b"IMG:\n")
                        ser.flush()
                        if wait_ok():
                            for i in range(0, len(img_bytes), 1024):
                                ser.write(img_bytes[i:i+1024])
                                ser.flush()
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
                
        except subprocess.CalledProcessError:
            ser.write(b"MUS:No Player|Idle|stopped\n")
            ser.write(b"PRG:0|0\n")
            for i in range(3): ser.write(f"LYR:{i}|...\n".encode())
        except Exception as e:
            print(f"Error in main loop: {e}")
            time.sleep(2)
        
        if current_page == 1:
            try:
                cpu = psutil.cpu_percent(interval=None)
                ram = psutil.virtual_memory().percent
                disk = psutil.disk_usage('/').percent
                cpu_t, gpu_t = get_hw_temps()
                if not gpu_t or gpu_t == "0": gpu_t = "0"
                ser.write(f"SYS:{cpu:.0f}|{ram:.0f}|{disk:.0f}|{cpu_t}|{gpu_t}\n".encode())
            except: pass

        if current_page == 3:
            try:
                local_ip, pub_ip_val = get_network_info()
                ser.write(f"NET:{local_ip}|{pub_ip_val}\n".encode())
            except: pass
            
        time.sleep(1)

if __name__ == "__main__":
    main()