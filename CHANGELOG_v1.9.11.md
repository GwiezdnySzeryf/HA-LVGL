# HA Panel - Release Notes v1.9.11

## Summary of Changes / Podsumowanie zmian

### 1. Two-Step Update Interface (Dwuetapowy ekran aktualizacji)
- Replaced direct "Sprawdź i aktualizuj" button with a dedicated **"Sprawdź aktualizacje"** flow in `create_updates_screen()`.
- **Step 1**: User clicks "Sprawdź aktualizacje". The application queries the GitHub Releases API (`https://api.github.com/repos/GwiezdnySzeryf/HA-LVGL/releases/latest`) asynchronously and parses available assets.
- **Step 2**: 
  - If the panel is already running the latest version, the UI confirms: `✓ Posiadasz najnowszą wersję (v1.9.11)`.
  - If a new version is detected on GitHub, the UI updates status to `Dostępna nowa wersja!` and displays a confirmation button: **"Zaktualizuj do vX.Y.Z"**.

### 2. Multi-Asset OTA Download & Automated Wake Word Asset Sync
- Updated `perform_github_ota()` to download all release assets dynamically from GitHub:
  1. `ha_panel.tmp` -> Main application binary.
  2. `mww_worker.tmp` -> MicroWakeWord engine worker for RK3308 AArch64.
  3. `okay_nabu_v2.tflite.tmp` -> MicroWakeWord TFLite neural model.
- Automatically applies execution permissions (`chmod 755`) and performs atomic file replacement.

### 3. Audio & Process Cleanup for MicroWakeWord ("Okay Nabu")
- Added proactive process cleanup (`killall -9 mww_worker arecord 2>/dev/null`) before initializing `WakeWordListener`.
- Prevents ALSA PCM audio capture conflicts (`hw:0,0` / `Device or resource busy`) caused by dangling or background processes after OTA updates.
- Updated `ensure_wake_word_assets()` to fetch release assets dynamically corresponding to `CURRENT_VERSION`.

---

### Files Modified:
- `src/main.cpp`: Updated `CURRENT_VERSION` to `v1.9.11`, implemented `ReleaseInfo` parsing, `check_github_release()`, multi-asset `perform_github_ota()`, two-step UI, and process cleanup in `start_wake_word_listener()`.
- `CHANGELOG_v1.9.11.md`: Created release documentation.
