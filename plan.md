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
- [ ] Dalsza optymalizacja i dopracowanie podmenu Wi-Fi (poprawa obsługi list sieci, zarządzania zapisanymi profilami i ponownego łączenia).
- [ ] Stworzyć widok siatki kafelków (Grid layout) na ekranie głównym.
- [ ] Dodać wsparcie dla ikonek MDI (Material Design Icons) dla różnych typów urządzeń.
- [ ] Umożliwić dodawanie dowolnej liczby przycisków za pośrednictwem portalu WWW.
- [ ] Dodać obsługę termostatów i wskaźników temperatury.

### Faza 2: Komunikacja w Czasie Rzeczywistym - WebSocket / MQTT (v2.0.0)
- [ ] Zastąpić cykliczne odpytywanie HTTP (polling 5s) połączeniem WebSocket / MQTT.
- [ ] Wdrożyć natychmiastowe reakcje ekranu na zmiany stanu w Home Assistant.
- [ ] Dodać sterowanie opcjonalnymi automatyzacjami i scenami (`scene.turn_on`).

### Faza 3: Dźwięk, Mikrofon i Asystent Głosowy (v2.2.0)
- [x] Zweryfikować formaty ALSA wbudowanego mikrofonu i głośnika; sprzęt przechwytuje 16 kHz stereo, a Assist użyje pierwszego kanału jako mono. Wyniki: `docs/home-assistant-lvgl-app/oem-audio-reuse.md`.
- [ ] Zmierzyć jakość mikrofonu, opóźnienie, poziom szumu oraz wpływ odtwarzania przez głośnik na nagranie.
- [ ] Wdrożyć lekkiego, natywnego satelitę Home Assistant Assist korzystającego z aktualnego protokołu ESPHome Voice Assistant; nie opierać nowej implementacji na zarchiwizowanym `wyoming-satellite`.
- [ ] Najpierw uruchomić tryb push-to-talk, przesyłanie głosu do HA oraz odtwarzanie odpowiedzi TTS.
- [ ] Dodać zdalne wykrywanie słowa wybudzającego przez Home Assistant, bez stałego lokalnego obciążenia panelu modelem AI.
- [ ] Ocenić i wdrożyć AGC, redukcję szumu oraz eliminację echa akustycznego przed włączeniem nasłuchu podczas odpowiedzi głośnika.
- [ ] Dopiero po benchmarkach rozważyć lokalne microWakeWord/OpenWakeWord; gotowy Linux Voice Assistant przekracza dostępne zasoby panelu.
- [ ] Dodać obsługę stanów rozmowy, timerów, kontynuacji konwersacji i błędów w interfejsie LVGL.
- [ ] Zaimplementować funkcje podmenu Mikrofon (obecnie ekran TODO).
- [ ] Zastąpić ekran TODO Asystenta ustawieniami prywatności mikrofonu, trybu aktywacji, głośności odpowiedzi i wybranego pipeline Assist.

### Faza 4: Sendspin - muzyka, ekran i multi-room (v2.4.0)
- [x] Poprawić nazwę podmenu z `Senspin` na `Sendspin`.
- [ ] Zintegrować `sendspin-cpp` i CMake z obecnym statycznym buildem AArch64; podnieść standard wybranych modułów do C++20.
- [ ] Najpierw wdrożyć role `controller`, `metadata` i `artwork`: stan odtwarzania, tytuł, wykonawca, okładka, postęp i przyciski transportu.
- [ ] Dodać ekran Now Playing oraz sterowanie play/pause, poprzedni/następny, głośność i wyciszenie w LVGL.
- [ ] Zaimplementować mDNS `_sendspin._tcp`, serwer WebSocket na porcie 8928 oraz trwały identyfikator klienta.
- [ ] Dodać rolę `player` w minimalnym wariancie PCM 48 kHz, 16-bit, mono z ograniczonym buforem dostosowanym do pamięci panelu.
- [ ] Napisać wyjście ALSA raportujące rzeczywisty moment odtworzenia próbek przez `notify_audio_played()` i skalibrować stałe opóźnienie sprzętu.
- [ ] Po stabilizacji PCM ocenić koszt FLAC i Opus; nie dodawać kodeków, jeśli binarka przekroczy bezpieczny budżet partycji `/tuya/data`.
- [ ] Przetestować synchronizację multi-room, dryf zegara, underruny, utratę Wi-Fi, ponowne połączenie i współdzielenie głośnika z Assist.
- [ ] Zachować możliwość wyłączenia Sendspin i powrotu do poprzedniej binarki, ponieważ protokół jest nadal w public preview.

---

## 🔒 Zasady Bezpieczeństwa
- **Brak sekretów w Git**: Pliki `ha_config.json`, `.env`, tokeny i klucze są bezwzględnie ignorowane przez `.gitignore`.
- **Maskowanie w Interfejsie**: Tokeny dostępu na stronie WWW pozostają zamaskowane (`type="password"`), a API `status.sh` zwraca wyłącznie flagę `ha_token_set`.
