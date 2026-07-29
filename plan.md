# Plan Rozwoju Projektu (Roadmap & Architecture)
## TPP01_HA_Panel — Smart Home Panel TPP01-Z

Niniejszy dokument opisuje architekturę systemu, obecny stan oraz szczegółowy plan rozwoju oprogramowania.

---

## 🏗️ Architektura Systemu

1. **Warstwa Sprzętowa i Systemowa**:
   - **Sprzęt**: Tuya MOES TPP01-Z (4-calowy ekran dotykowy 480x480 pixel, SoC AArch64).
   - **System**: Embedded Linux (Kernel 4.9, BusyBox ash, glibc 2.23).
   - **Wyświetlacz i Dotyk**: Bezpośredni Framebuffer `/dev/fb0` (mapowanie ioctl) + sterownik zdarzeń dotykowych `/dev/input/event0`.

2. **Aplikacja Główna (`ha_panel`)**:
   - **Język i Biblioteka**: C++11, LVGL v8.3.11 (kompilacja statyczna `-static`).
   - **Komunikacja HA**: Silnik HTTP/HTTPS oparty na `/tuya/data/curl` (z wywołaniem nagłówków Bearer Token).
   - **Konfiguracja**: Przechowywana w bezpiecznym pliku `/tuya/data/ha_config.json` (prawa `0600`).

3. **Portal Zarządzania WWW**:
   - **Serwer**: BusyBox `httpd` (port 80).
   - **Skrypty CGI**: `/tuya/data/www/cgi-bin/` (`status.sh`, `save_config.sh`, `action.sh`, `screenshot.sh`).
   - **Podgląd żywy**: Konwersja surowego bufora `/dev/fb0` na Canvas HTML5 w przeglądarce.

---

## 📋 Plan Działań i Zadania (Checklist)

### Faza 1: Rozbudowa Interfejsu Polskiego i Siatki Encji (v1.8.0)
- [ ] Stworzyć widok siatki kafelków (Grid layout) na ekranie głównym.
- [ ] Dodać wsparcie dla ikonek MDI (Material Design Icons) dla różnych typów urządzeń.
- [ ] Umożliwić dodawanie dowolnej liczby przycisków za pośrednictwem portalu WWW.
- [ ] Dodać obsługę termostatów i wskaźników temperatury.

### Faza 2: Komunikacja w Czasie Rzeczywistym - WebSocket / MQTT (v2.0.0)
- [ ] Zastąpić cykliczne odpytywanie HTTP (polling 5s) połączeniem WebSocket / MQTT.
- [ ] Wdrożyć natychmiastowe reakcje ekranu na zmiany stanu w Home Assistant.
- [ ] Dodać sterowanie opcjonalnymi automatyzacjami i scenami (`scene.turn_on`).

### Faza 3: Dźwięk, Mikrofon i Asystent Głosowy (v2.2.0)
- [ ] Skonfigurować sterowniki ALSA dla wbudowanego mikrofonu i głośnika.
- [ ] Wdrożyć klienta Wyoming Satellite dla Home Assistant Assist.
- [ ] Umożliwić lokalne detekcje słowa wybudzającego (OpenWakeWord).

---

## 🔒 Zasady Bezpieczeństwa
- **Brak sekretów w Git**: Pliki `ha_config.json`, `.env`, tokeny i klucze są bezwzględnie ignorowane przez `.gitignore`.
- **Maskowanie w Interfejsie**: Tokeny dostępu na stronie WWW pozostają zamaskowane (`type="password"`), a API `status.sh` zwraca wyłącznie flagę `ha_token_set`.
