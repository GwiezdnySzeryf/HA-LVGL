# Kamienie Milowe Projektu (Milestones)
## MOES TPP01-Z Smart Home Panel | Native HA LVGL

Tabela przedstawia przebieg prac nad oprogramowaniem i architekturą panelu TPP01-Z.

---

### 🏁 Osiągnięte Kamienie Milowe

#### 🚀 v1.0.0 – Reverse Engineering & Native LVGL Core
- [x] Opracowanie sterowników linuksowych Framebuffer (`/dev/fb0`) dla ekranu 480x480 60Hz.
- [x] Implementacja obsługi ekranu dotykowego z kalibracją układu współrzędnych.
- [x] Portowanie biblioteki LVGL v8.3.11 C++ do pracy bez środowiska graficznego (bare Linux).
- [x] Wyświetlanie natywnego logo Home Assistant oraz prostego interfejsu.

#### 💡 v1.2.0 – Integracja Home Assistant REST API
- [x] Obsługa protokołu HTTP/REST do komunikacji z API Home Assistant.
- [x] Sterowanie dwiema głównymi encjami (`light`, `switch`, `fan`, `input_boolean`).
- [x] Automatyczne odświeżanie i odpytywanie stanu encji (polling timer).
- [x] Ekran wygaszacza ekranu z wygaszaniem podświetlenia po 30s nieaktywności.

#### 🎛️ v1.3.0 – Centrum Sterowania & Pasek Gestów
- [x] Implementacja wysuwanego od góry Paska / Centrum Sterowania (Control Center).
- [x] Suwak regulacji jasności ekranu (bezpośredni zapis do `/sys/class/backlight`).
- [x] Suwak regulacji głośności dźwięku (sterowanie sterownikiem `amixer`).
- [x] Przycisk szybkiego przejścia do Ustawień systemowych.

#### 🌐 v1.5.0 – Portal Zarządzania WWW & Onboarding
- [x] Wdrożenie serwera WWW BusyBox `httpd` na porcie 80.
- [x] Strona konfiguracyjna `/config.html` oraz generowanie natywnego kodu QR na ekranie panelu.
- [x] Podgląd ekranu urządzenia na żywo w przeglądarce (`/dev/fb0` -> Canvas HTML5).
- [x] Skrypty CGI Shell (`status.sh`, `action.sh`, `save_config.sh`) do obsługi formularzy.

#### 📦 v1.7.0 – Pełne HTTPS/SSL, Bezpieczeństwo i Dedykowane Ekrany Systemowe
- [x] **Wsparcie dla HTTPS/SSL**: Integracja statycznego silnika `curl` do obsługi połączeń szyfrowanych SSL/TLS 1.2/1.3.
- [x] **Ekrany Dedykowane w Ustawieniach Systemowych**:
  - `Aktualizacje`: Wersja oprogramowania, źródło wydań, stan połączenia i przycisk OTA.
  - `Diagnostyka`: Proces PID, sieć/IP, Home Assistant API, stan portalu WWW, odświeżanie.
  - `Informacje`: Model panelu, wersja, silnik graficzny LVGL, informacje o projekcie.
- [x] **Aktualizacje OTA**: Automatyczne pobieranie wydań z GitHub Releases z paskiem postępu.
- [x] **Bezpieczeństwo Tokenów**: Maskowanie tokenu w interfejsie WWW (`type="password"`, ukrywanie w `status.sh`, zachowywanie przy edycji).
- [x] **Przycisk Rozłączenia z HA**: Bezpieczne czyszczenie konfiguracji z okienkiem potwierdzenia.
- [x] **Automatyczny Restart**: Automatyczny przeładowanie aplikacji po zapisaniu ustawień.

---

### 🎯 Planowane Kamienie Milowe

#### 📱 v1.8.0 – Wielostronicowe Pulpity i Siatka Encji (W trakcie)
- [ ] Obsługa więcej niż 2 encji (pulpit siatki 2x2 / 2x3).
- [ ] Przełączanie między wieloma kartami/pomieszczeniami (swipe lewo/prawo).
- [ ] Sterowanie temperaturą (termostat / climate tile).

#### ⚡ v2.0.0 – Dwukierunkowy Protokół MQTT / WebSocket
- [ ] Integracja z brokerem MQTT (instantaniczna synchronizacja stanu bez odpytywania HTTP).
- [ ] Publikowanie statusu panelu (stan wygaszenia, jasność, temperatura) do HA.
- [ ] Obsługa wiadomości powiadomień (Push Notifications) z HA na ekran panelu.

#### 🎙️ v2.2.0 – Asystent Głosowy & Wyoming Protocol
- [ ] Wykorzystanie wbudowanego mikrofonu i głośnika panelu.
- [ ] Integracja z Home Assistant Voice / Wyoming Satellite.
- [ ] Słowo wybudzające (Wake Word) i komendy głosowe.
