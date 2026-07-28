#!/usr/bin/env python3
"""
MOES TPP01-Z | Panel Control & Deployment TUI
Authored by OpenCode for HA-LVGL project
"""

import os
import sys
import time
import socket
import subprocess

# ANSI Colors
C_RESET = "\033[0m"
C_BOLD = "\033[1m"
C_RED = "\033[31m"
C_GREEN = "\033[32m"
C_YELLOW = "\033[33m"
C_BLUE = "\033[34m"
C_MAGENTA = "\033[35m"
C_CYAN = "\033[36m"
C_BG_DARK = "\033[40m"

SERIAL = "100211471004F0"
DEFAULT_IP = "192.168.1.111"

def discover_panel_ip():
    global DEFAULT_IP
    for test_ip in [DEFAULT_IP, "192.168.1.140", "192.168.1.111"]:
        try:
            s = socket.socket()
            s.settimeout(0.2)
            s.connect((test_ip, 23))
            s.close()
            DEFAULT_IP = test_ip
            return test_ip
        except Exception:
            pass

    import concurrent.futures
    def check_ip(i):
        ip = f"192.168.1.{i}"
        try:
            s = socket.socket()
            s.settimeout(0.2)
            s.connect((ip, 23))
            s.close()
            return ip
        except Exception:
            return None

    with concurrent.futures.ThreadPoolExecutor(max_workers=50) as ex:
        found = [r for r in ex.map(check_ip, range(1, 255)) if r]

    if found:
        DEFAULT_IP = found[0]
        return found[0]

    return DEFAULT_IP

def clear_screen():
    os.system("clear" if os.name != "nt" else "cls")

def print_header(status_text=""):
    clear_screen()
    print(f"{C_BOLD}{C_CYAN}====================================================={C_RESET}")
    print(f"{C_BOLD}{C_GREEN} 🏡 MOES TPP01-Z | Native HA Panel Management TUI    {C_RESET}")
    print(f"{C_BOLD}{C_CYAN}====================================================={C_RESET}")
    if status_text:
        print(f" Status: {C_YELLOW}{status_text}{C_RESET}")
        print(f"{C_CYAN}-----------------------------------------------------{C_RESET}")

def run_adb_cmd(cmd_bytes, timeout=5):
    """Executes a shell command via ADB socket 5037 with auto-login."""
    try:
        sock = socket.create_connection(("127.0.0.1", 5037), timeout=timeout)
        payload = f"host:transport:{SERIAL}".encode("utf-8")
        sock.sendall(f"{len(payload):04x}".encode("ascii") + payload)
        if sock.recv(4) != b"OKAY":
            sock.close()
            return None

        payload = b"shell:"
        sock.sendall(f"{len(payload):04x}".encode("ascii") + payload)
        if sock.recv(4) != b"OKAY":
            sock.close()
            return None

        buffer = b""
        start_time = time.time()
        sent = False

        while time.time() - start_time < timeout:
            chunk = sock.recv(4096)
            if not chunk: break
            buffer += chunk

            if b"login:" in buffer:
                sock.sendall(b"root\n")
                buffer = b""
            elif b"Password:" in buffer:
                sock.sendall(b"admin\n")
                buffer = b""
            elif b"#" in buffer:
                if not sent:
                    time.sleep(0.2)
                    sock.sendall(cmd_bytes + b"\n")
                    sent = True
                    buffer = b""
                else:
                    break

        res = buffer.decode("utf-8", errors="ignore")
        sock.close()
        return res
    except Exception as e:
        return f"Error: {e}"

def run_telnet_cmd(cmd_bytes, ip=DEFAULT_IP, timeout=4):
    """Executes a shell command via raw netcat/telnet port 23 over Wi-Fi."""
    try:
        sock = socket.create_connection((ip, 23), timeout=timeout)
        sock.settimeout(1.5)
        
        # Send command immediately in case it is raw netcat shell
        sock.sendall(cmd_bytes + b"\n")
        
        time.sleep(0.3)
        buffer = b""
        start_time = time.time()

        while time.time() - start_time < timeout:
            try:
                chunk = sock.recv(4096)
                if not chunk: break
                buffer += chunk

                if b"login:" in buffer:
                    sock.sendall(b"root\n")
                    buffer = b""
                elif b"Password:" in buffer:
                    sock.sendall(b"admin\n")
                    buffer = b""
                elif b"#" in buffer or len(buffer) > 100:
                    break
            except socket.timeout:
                if buffer: break

        res = buffer.decode("utf-8", errors="ignore")
        sock.close()
        return res
    except Exception:
        return None

def run_panel_cmd(cmd_bytes, timeout=5):
    """Tries ADB USB first, falls back seamlessly to Wi-Fi Telnet."""
    res = run_adb_cmd(cmd_bytes, timeout=timeout)
    if res and not res.startswith("Error:"):
        return res
    telnet_res = run_telnet_cmd(cmd_bytes, timeout=timeout)
    if telnet_res:
        return telnet_res
    return res or "Brak połączenia (ADB i Telnet nieodpowiadają)"

def check_process_status():
    res = run_panel_cmd(b"ps w | grep -v grep | grep -E 'ha_panel|voice_control|httpd'")
    if not res: return "Brak połączenia z panelem"
    
    status = []
    if "ha_panel" in res:
        status.append(f"{C_GREEN}ha_panel (AKTYWNY){C_RESET}")
    else:
        status.append(f"{C_RED}ha_panel (INAKTYWNY){C_RESET}")

    if "httpd" in res:
        status.append(f"{C_GREEN}Portal WWW (ON){C_RESET}")
    else:
        status.append(f"{C_YELLOW}Portal WWW (OFF){C_RESET}")

    if "voice_control" in res:
        status.append(f"{C_MAGENTA}Tuya GUI (Odpala w tle){C_RESET}")

    return " | ".join(status)

def monitor_startup(duration_sec=5):
    """Monitors process state and live logs for a few seconds post-launch."""
    print(f"\n{C_CYAN}=== Monitorowanie startu aplikacji ({duration_sec}s) ==={C_RESET}")

    for i in range(1, duration_sec + 1):
        time.sleep(1)
        res_ps = run_panel_cmd(b"ps w | grep -v grep")
        res_log = run_panel_cmd(b"cat /tmp/ha_panel.log 2>/dev/null | tail -n 3")

        ha_running = False
        safe_mode = False

        if res_ps:
            for line in res_ps.splitlines():
                if "grep" in line: continue
                if "/tuya/data/ha_panel" in line or " ha_panel" in line:
                    if " S " in line or " R " in line:
                        ha_running = True
                if "voice_control_safe_mode" in line:
                    if " S " in line or " R " in line:
                        safe_mode = True

        status_str = f"{C_GREEN}AKTYWNY{C_RESET}" if ha_running else f"{C_RED}NIEAKTYWNY{C_RESET}"
        if safe_mode:
            status_str += f" | {C_RED}OSTRZEŻENIE: Wykryto Safe Mode!{C_RESET}"

        print(f" [{i}/{duration_sec}s] Stan ha_panel: {status_str}")

        if res_log and res_log.strip():
            lines = [line.strip() for line in res_log.strip().splitlines() if line.strip() and not line.startswith("cat ") and not line.startswith("#")]
            if lines:
                print(f"     Ostatnie logi: {C_YELLOW}{' | '.join(lines)}{C_RESET}")

    print(f"{C_CYAN}====================================================={C_RESET}\n")

def start_application():
    print(f"\n{C_YELLOW}Uruchamianie aplikacji /tuya/data/ha_panel na panelu...{C_RESET}")
    cmd = (
        b"rm -f /tmp/safe_mode_triggered /tmp/safe_mode_record 2>/dev/null; "
        b"killall -STOP tuya_monitor.sh 2>/dev/null; "
        b"killall -9 voice_control voice_control_safe_mode ha_panel 2>/dev/null; "
        b"echo 255 > /sys/class/backlight/backlight/brightness; "
        b"chmod +x /tuya/data/ha_panel; "
        b"nohup /tuya/data/ha_panel > /tmp/ha_panel.log 2>&1 &"
    )
    run_panel_cmd(cmd)
    monitor_startup(5)
    input("Naciśnij Enter, aby kontynuować...")

def stop_application():
    print(f"\n{C_YELLOW}Zatrzymywanie aplikacji ha_panel...{C_RESET}")
    run_panel_cmd(b"killall -9 ha_panel 2>/dev/null")
    print(f"{C_GREEN}Aplikacja została zatrzymana.{C_RESET}")
    time.sleep(1)

def build_and_deploy():
    print(f"\n{C_YELLOW}1. Kompilacja projektu C++ / LVGL (make)...{C_RESET}")
    ret = subprocess.run(["make", "-j4"], cwd="/home/tomasz/OpenCode/Inne/TPP01_HA_Panel")
    if ret.returncode != 0:
        print(f"{C_RED}Błąd kompilacji! Sprawdź kod źródłowy.{C_RESET}")
        input("\nNaciśnij Enter, aby kontynuować...")
        return

    print(f"\n{C_YELLOW}2. Przesyłanie binarki /tuya/data/ha_panel przez ADB...{C_RESET}")
    ret = subprocess.run(["adb", "-s", SERIAL, "push", "/home/tomasz/OpenCode/Inne/TPP01_HA_Panel/ha_panel", "/tuya/data/ha_panel"])
    if ret.returncode != 0:
        print(f"{C_RED}Błąd przesyłania ADB push!{C_RESET}")
        input("\nNaciśnij Enter, aby kontynuować...")
        return

    print(f"\n{C_YELLOW}3. Przesyłanie plików portalu WWW (/tuya/data/www/)...{C_RESET}")
    subprocess.run(["adb", "-s", SERIAL, "push", "/home/tomasz/OpenCode/Inne/TPP01_HA_Panel/www/index.html", "/tuya/data/www/index.html"])
    subprocess.run(["adb", "-s", SERIAL, "push", "/home/tomasz/OpenCode/Inne/TPP01_HA_Panel/www/cgi-bin/screenshot.sh", "/tuya/data/www/cgi-bin/screenshot.sh"])
    subprocess.run(["adb", "-s", SERIAL, "push", "/home/tomasz/OpenCode/Inne/TPP01_HA_Panel/www/cgi-bin/status.sh", "/tuya/data/www/cgi-bin/status.sh"])
    subprocess.run(["adb", "-s", SERIAL, "push", "/home/tomasz/OpenCode/Inne/TPP01_HA_Panel/www/cgi-bin/action.sh", "/tuya/data/www/cgi-bin/action.sh"])

    start_application()

def toggle_web_portal():
    print(f"\n{C_YELLOW}Przełączanie stanu Portalu WWW (httpd)...{C_RESET}")
    cmd = (
        "if pidof httpd >/dev/null; then "
        "  killall -9 httpd; echo 'Portal WWW Wylaczony'; "
        "else "
        "  chmod +x /tuya/data/www/cgi-bin/* 2>/dev/null; "
        "  httpd -h /tuya/data/www -p 80 & echo 'Portal WWW Wlaczony na porcie 80'; "
        "fi"
    ).encode("utf-8")
    res = run_panel_cmd(cmd)
    print(f"{C_GREEN}{res}{C_RESET}")
    time.sleep(1.5)

def capture_screenshot():
    print(f"\n{C_YELLOW}Pobieranie zrzutu ekranu /dev/fb0 z panelu...{C_RESET}")
    raw_path = "/tmp/panel_fb0.raw"
    raw_data = None

    # Try HTTP CGI endpoint first
    try:
        import urllib.request
        with urllib.request.urlopen(f"http://{DEFAULT_IP}/cgi-bin/screenshot.sh", timeout=5) as resp:
            raw_data = resp.read()
    except Exception:
        pass

    # Fallback to ADB pull if HTTP endpoint failed
    if not raw_data or len(raw_data) < 921600:
        subprocess.run(["adb", "-s", SERIAL, "pull", "/dev/fb0", raw_path], stdout=subprocess.DEVNULL)
        if os.path.exists(raw_path):
            with open(raw_path, "rb") as f:
                raw_data = f.read()

    if raw_data and len(raw_data) >= 921600:
        with open(raw_path, "wb") as f:
            f.write(raw_data[:921600])

        png_path = "/home/tomasz/OpenCode/Inne/TPP01_HA_Panel/screenshots/screenshot_latest.png"
        os.makedirs(os.path.dirname(png_path), exist_ok=True)
        # Convert raw BGRA 480x480 to PNG using ffmpeg
        subprocess.run([
            "ffmpeg", "-y", "-f", "rawvideo", "-pixel_format", "bgra",
            "-video_size", "480x480", "-i", raw_path, png_path
        ], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print(f"{C_GREEN}Zapisano zrzut ekranu: {png_path}{C_RESET}")
    else:
        print(f"{C_RED}Błąd pobierania bufora ramki!{C_RESET}")
    input("\nNaciśnij Enter, aby kontynuować...")

def view_logs():
    print(f"\n{C_YELLOW}Odczytywanie logów /tmp/ha_panel.log...{C_RESET}")
    res = run_panel_cmd(b"cat /tmp/ha_panel.log | tail -n 30")
    print(f"{C_CYAN}--- LOGI (Ostatnie 30 linii) ---{C_RESET}")
    print(res)
    input("\nNaciśnij Enter, aby kontynuować...")

def main_menu():
    while True:
        status = check_process_status()
        print_header(status)
        print(f" {C_BOLD}1.{C_RESET} 🚀 Uruchom / Wznów Aplikację ({C_GREEN}ha_panel{C_RESET})")
        print(f" {C_BOLD}2.{C_RESET} ⏹️  Zatrzymaj Aplikację")
        print(f" {C_BOLD}3.{C_RESET} 🔨 Kompiluj & Wgraj nową wersję ({C_YELLOW}make + adb push{C_RESET})")
        print(f" {C_BOLD}4.{C_RESET} 🌐 Przełącz Portal WWW ({C_CYAN}httpd port 80{C_RESET})")
        print(f" {C_BOLD}5.{C_RESET} 📸 Przechwyć Zrzut Ekranu ({C_MAGENTA}Screenshot FB0{C_RESET})")
        print(f" {C_BOLD}6.{C_RESET} 📄 Pokaż Logi Aplikacji ({C_BLUE}/tmp/ha_panel.log{C_RESET})")
        print(f" {C_BOLD}7.{C_RESET} 🔌 Zrestartuj Panel ({C_RED}Reboot{C_RESET})")
        print(f" {C_BOLD}0.{C_RESET} 🚪 Wyjście")
        print(f"{C_CYAN}-----------------------------------------------------{C_RESET}")

        choice = input("Wybierz opcję [0-7]: ").strip()

        if choice == "1":
            start_application()
        elif choice == "2":
            stop_application()
        elif choice == "3":
            build_and_deploy()
        elif choice == "4":
            toggle_web_portal()
        elif choice == "5":
            capture_screenshot()
        elif choice == "6":
            view_logs()
        elif choice == "7":
            if input("Czy na pewno zrestartować urządzenie? (t/N): ").lower() == "t":
                run_panel_cmd(b"reboot")
                print(f"{C_RED}Wysłano polecenie reboot.{C_RESET}")
                time.sleep(2)
        elif choice == "0":
            print(f"\n{C_GREEN}Do widzenia!{C_RESET}")
            sys.exit(0)

if __name__ == "__main__":
    try:
        main_menu()
    except KeyboardInterrupt:
        print(f"\n{C_GREEN}Przerwano przez użytkownika.{C_RESET}")
        sys.exit(0)
