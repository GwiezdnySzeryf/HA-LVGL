#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include "../lvgl/lvgl.h"
#include "../lvgl/src/extra/libs/qrcode/lv_qrcode.h"

// HAL declarations
bool hal_display_init(void);
bool hal_touch_init(void);
void hal_shutdown(void);

// Global configuration variables
std::string ha_url = "";
std::string ha_token = "";
bool onboarding_active = false;

// Version of current binary
const char * CURRENT_VERSION = "v1.5.0";

static lv_obj_t * control_center = NULL;
static lv_obj_t * brightness_value_label = NULL;
static lv_obj_t * volume_value_label = NULL;
static int control_center_drag_start_y = 0;
static int control_center_drag_start_panel_y = -480;
static bool control_center_drag_active = false;
static int control_center_drag_last_y = 0;
static uint32_t control_center_drag_last_time = 0;
static int control_center_drag_velocity = 0;
static int backlight_max = 255;
static lv_obj_t * settings_screen = NULL;

// Declare external native image data
extern const lv_img_dsc_t ha_logo;
LV_FONT_DECLARE(lv_font_control_icons_24);

#define ICON_BRIGHTNESS "\xEF\x86\x85"
#define ICON_VOLUME     "\xEF\x80\xA8"
#define ICON_SETTINGS   "\xEF\x80\x93"
#define ICON_HOME       "\xEF\x80\x95"
#define ICON_DOWNLOAD   "\xEF\x80\x99"
#define ICON_CHEVRON    "\xEF\x81\x94"
#define ICON_BACK       "\xEF\x81\xA0"
#define ICON_GLOBE      "\xEF\x82\xAC"
#define ICON_TOOLS      "\xEF\x82\xAD"
#define ICON_DISPLAY    "\xEF\x84\x88"
#define ICON_INFO       "\xEF\x84\xA9"
#define ICON_MIC        "\xEF\x84\xB0"
#define ICON_PLUG       "\xEF\x87\xA6"
#define ICON_WIFI       "\xEF\x87\xAB"
#define ICON_BLUETOOTH  "\xEF\x8A\x93"
#define ICON_PALETTE    "\xEF\x94\xBF"

// Helper function to get wlan0 IP address dynamically
std::string get_wlan0_ip() {
    int fd;
    struct ifreq ifr;
    std::string ip = "127.0.0.1";

    fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd >= 0) {
        ifr.ifr_addr.sa_family = AF_INET;
        strncpy(ifr.ifr_name, "wlan0", IFNAMSIZ-1);
        if (ioctl(fd, SIOCGIFADDR, &ifr) == 0) {
            ip = inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr);
        }
        close(fd);
    }
    return ip;
}

// Check if config file exists
bool config_exists() {
    return access("/tuya/data/ha_config.json", F_OK) == 0;
}

// Lightweight JSON value parser using string searches to avoid heavy parser link dependencies
std::string parse_json_value(const std::string &json, const std::string &key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";
    
    size_t colon_pos = json.find(":", key_pos);
    if (colon_pos == std::string::npos) return "";
    
    size_t start_quote = json.find("\"", colon_pos);
    if (start_quote == std::string::npos) return "";
    
    size_t end_quote = json.find("\"", start_quote + 1);
    if (end_quote == std::string::npos) return "";
    
    return json.substr(start_quote + 1, end_quote - start_quote - 1);
}

// Load config file values
bool load_configuration() {
    std::ifstream f("/tuya/data/ha_config.json");
    if (!f.is_open()) return false;
    
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string json = buffer.str();
    
    ha_url = parse_json_value(json, "ha_url");
    ha_token = parse_json_value(json, "ha_token");
    
    return !ha_url.empty();
}

// Perform OTA self-replacement update directly from GitHub Releases API
bool perform_github_ota() {
    printf("[OTA] Starting OTA process...\n");
    
    // 1. Fetch latest release info via secure wget command from public repo
    std::string cmd_fetch = "wget -qO- --header=\"User-Agent: Mozilla/5.0\" "
                            "https://api.github.com/repos/GwiezdnySzeryf/HA-LVGL/releases/latest > /tmp/latest_release.json";
    
    printf("[OTA] Querying GitHub Releases API...\n");
    system(cmd_fetch.c_str());
    
    // Read the downloaded release JSON
    std::ifstream f("/tmp/latest_release.json");
    if (!f.is_open()) {
        printf("[OTA] Error: Failed to download release JSON.\n");
        return false;
    }
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string json = buffer.str();
    unlink("/tmp/latest_release.json");
    
    std::string tag_name = parse_json_value(json, "tag_name");
    std::string download_url = parse_json_value(json, "browser_download_url");
    
    if (tag_name.empty() || download_url.empty()) {
        printf("[OTA] Error: Failed to parse tag_name or download URL. Releases might be empty.\n");
        return false;
    }
    
    printf("[OTA] Latest version: %s (Current: %s)\n", tag_name.c_str(), CURRENT_VERSION);
    if (tag_name == CURRENT_VERSION) {
        printf("[OTA] Panel is already up to date!\n");
        return false;
    }
    
    // 2. Download the binary asset from public URL
    printf("[OTA] Downloading new binary asset from %s...\n", download_url.c_str());
    std::string cmd_download = "wget -q --header=\"User-Agent: Mozilla/5.0\" "
                               "-O /tuya/data/ha_panel.tmp " + download_url;
                               
    int ret = system(cmd_download.c_str());
    if (ret != 0 || access("/tuya/data/ha_panel.tmp", F_OK) != 0) {
        printf("[OTA] Error: Failed to download binary asset.\n");
        return false;
    }
    
    // 3. Unix Self-Replacement
    printf("[OTA] Performing self-replacement...\n");
    chmod("/tuya/data/ha_panel.tmp", 0755);
    
    // Rename current binary to .old (this releases kernel execution lock!)
    rename("/tuya/data/ha_panel", "/tuya/data/ha_panel.old");
    
    // Rename temp binary to the main target path
    if (rename("/tuya/data/ha_panel.tmp", "/tuya/data/ha_panel") != 0) {
        // Rollback on failure
        rename("/tuya/data/ha_panel.old", "/tuya/data/ha_panel");
        printf("[OTA] Error: Failed to replace main binary.\n");
        return false;
    }
    
    // 4. Exec new binary to reload the app in-place!
    printf("[OTA] Success! Executing new binary in-place...\n");
    char *args[] = {(char *)"/tuya/data/ha_panel", NULL};
    execv(args[0], args);
    
    return true; // Unreachable if execv succeeds
}

// Event callback for native LVGL buttons
static void btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * btn = lv_event_get_target(e);
    
    if (code == LV_EVENT_CLICKED) {
        bool state = lv_obj_has_state(btn, LV_STATE_CHECKED);
        
        if (state) {
            lv_obj_set_style_bg_color(btn, lv_color_make(0x00, 0xAA, 0x00), LV_PART_MAIN); // Green for ON
            printf("[HA Click] Light turned ON (REST/Websocket request simulation to %s)!\n", ha_url.c_str());
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN); // Red for OFF
            printf("[HA Click] Light turned OFF (REST/Websocket request simulation to %s)!\n", ha_url.c_str());
        }
    }
}

static void ota_update_timer_cb(lv_timer_t * timer) {
    lv_obj_t * mbox = (lv_obj_t *)timer->user_data;
    lv_timer_del(timer);

    if (!perform_github_ota()) {
        lv_msgbox_close(mbox);
        lv_msgbox_create(NULL, "BŁĄD", "Aktualizacja nie powiodła się!\nSprawdź połączenie.", NULL, true);
    }
}

// Event callback for the Update modal buttons
static void ota_msgbox_cb(lv_event_t * e) {
    lv_obj_t * obj = lv_event_get_current_target(e);
    uint16_t btn_id = lv_msgbox_get_active_btn(obj);
    
    if (btn_id == 0) { // "AKTUALIZUJ" (Update)
        printf("[OTA Click] Triggering OTA update...\n");
        lv_msgbox_close(obj);
        
        // Show temporary download modal on screen
        lv_obj_t * mbox = lv_msgbox_create(NULL, "AKTUALIZACJA", "Pobieranie nowej wersji z GitHuba...", NULL, false);
        lv_obj_align(mbox, LV_ALIGN_CENTER, 0, 0);
        lv_timer_create(ota_update_timer_cb, 150, mbox);
    } else { // "ZAMKNIJ" (Close)
        lv_msgbox_close(obj);
    }
}

static void show_info_popup(void) {
    static const char * btns[] = {"AKTUALIZUJ", "ZAMKNIJ", ""};

    std::string ip = get_wlan0_ip();
    std::stringstream ss;
    ss << "Wersja: " << CURRENT_VERSION << "\nAdres IP: " << ip;
    ss << "\n\nAktualizacje: GitHub";

    lv_obj_t * mbox = lv_msgbox_create(NULL, "INFORMACJE", ss.str().c_str(), btns, false);
    lv_obj_add_event_cb(mbox, ota_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(mbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(mbox, 360);
    lv_obj_set_style_bg_color(mbox, lv_color_make(0x2D, 0x2D, 0x2D), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_title(mbox), lv_color_make(0x03, 0xA9, 0xF4), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_text(mbox), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_btns(mbox), lv_color_make(0x20, 0x20, 0x20), LV_PART_ITEMS);
    lv_obj_set_width(lv_msgbox_get_btns(mbox), 320);
    lv_obj_set_style_pad_row(lv_msgbox_get_content(mbox), 10, LV_PART_MAIN);
}

// Event callback for the Info button "?"
static void info_btn_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_info_popup();
}

enum settings_action_t {
    SETTINGS_ACTION_NONE = 0,
    SETTINGS_ACTION_CONTROLS,
    SETTINGS_ACTION_DIAGNOSTICS,
    SETTINGS_ACTION_INFO
};

static void close_settings(void) {
    if (!settings_screen) return;
    lv_obj_del_async(settings_screen);
    settings_screen = NULL;
}

static void settings_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_settings();
}

static void diagnostics_msgbox_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_msgbox_close(lv_event_get_current_target(e));
    }
}

static void show_diagnostics_popup(void) {
    static const char * buttons[] = {"ZAMKNIJ", ""};
    std::string ip = get_wlan0_ip();
    std::stringstream ss;
    ss << "Aplikacja: aktywna\nSieć: ";
    ss << (ip == "127.0.0.1" ? "brak połączenia" : ip);
    ss << "\nHome Assistant: ";
    ss << (config_exists() ? "skonfigurowany" : "nieskonfigurowany");

    lv_obj_t * mbox = lv_msgbox_create(NULL, "DIAGNOSTYKA", ss.str().c_str(), buttons, false);
    lv_obj_add_event_cb(mbox, diagnostics_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_align(mbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(mbox, 360);
    lv_obj_set_style_bg_color(mbox, lv_color_make(0x2D, 0x2D, 0x2D), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_title(mbox), lv_color_make(0x03, 0xA9, 0xF4), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_text(mbox), lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_btns(mbox), lv_color_make(0x20, 0x20, 0x20), LV_PART_ITEMS);
}

static void settings_card_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_action_t action = (settings_action_t)(long)lv_event_get_user_data(e);

    if (action == SETTINGS_ACTION_CONTROLS) {
        close_settings();
        lv_obj_set_y(control_center, 0);
    } else if (action == SETTINGS_ACTION_DIAGNOSTICS) {
        show_diagnostics_popup();
    } else if (action == SETTINGS_ACTION_INFO) {
        show_info_popup();
    }
}

static void add_settings_section(lv_obj_t * list, const char * title, int * y) {
    lv_obj_t * label = lv_label_create(list);
    lv_label_set_text(label, title);
    lv_obj_set_pos(label, 32, *y + 6);
    lv_obj_set_style_text_color(label, lv_color_make(0x8A, 0xC7, 0xFA), LV_PART_MAIN);
    *y += 38;
}

static void add_settings_card(lv_obj_t * list, int * y, const char * icon_symbol,
                              lv_color_t icon_color, const char * title,
                              const char * subtitle, settings_action_t action) {
    lv_obj_t * card = lv_obj_create(list);
    lv_obj_set_size(card, 424, 78);
    lv_obj_set_pos(card, 28, *y);
    lv_obj_set_style_bg_color(card, lv_color_make(0x20, 0x23, 0x2B), LV_PART_MAIN);
    lv_obj_set_style_bg_color(card, lv_color_make(0x2A, 0x2E, 0x38), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * icon_bg = lv_obj_create(card);
    lv_obj_set_size(icon_bg, 48, 48);
    lv_obj_set_pos(icon_bg, 14, 15);
    lv_obj_set_style_bg_color(icon_bg, icon_color, LV_PART_MAIN);
    lv_obj_set_style_border_width(icon_bg, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(icon_bg, 24, LV_PART_MAIN);
    lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * icon = lv_label_create(icon_bg);
    lv_label_set_text(icon, icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_control_icons_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(icon, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * title_label = lv_label_create(card);
    lv_label_set_text(title_label, title);
    lv_obj_set_pos(title_label, 78, 13);
    lv_obj_set_style_text_color(title_label, lv_color_make(0xE4, 0xE2, 0xE9), LV_PART_MAIN);

    lv_obj_t * subtitle_label = lv_label_create(card);
    lv_label_set_text(subtitle_label, subtitle);
    lv_obj_set_pos(subtitle_label, 78, 43);
    lv_obj_set_style_text_color(subtitle_label, lv_color_make(0xA9, 0xA6, 0xB0), LV_PART_MAIN);

    if (action != SETTINGS_ACTION_NONE) {
        lv_obj_t * chevron = lv_label_create(card);
        lv_label_set_text(chevron, ICON_CHEVRON);
        lv_obj_set_style_text_font(chevron, &lv_font_control_icons_24, LV_PART_MAIN);
        lv_obj_set_style_text_color(chevron, lv_color_make(0x8F, 0x8D, 0x98), LV_PART_MAIN);
        lv_obj_align(chevron, LV_ALIGN_RIGHT_MID, -18, 0);
        lv_obj_add_event_cb(card, settings_card_event_cb, LV_EVENT_CLICKED, (void *)(long)action);
    } else {
        lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);
    }

    *y += 88;
}

static void create_settings_screen(void) {
    if (settings_screen) return;

    settings_screen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(settings_screen, 480, 480);
    lv_obj_set_pos(settings_screen, 0, 0);
    lv_obj_set_style_bg_color(settings_screen, lv_color_make(0x11, 0x13, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(settings_screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(settings_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(settings_screen, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(settings_screen, 0, LV_PART_MAIN);
    lv_obj_clear_flag(settings_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * back = lv_btn_create(settings_screen);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    lv_obj_set_style_bg_color(back, lv_color_make(0x2A, 0x2D, 0x35), LV_PART_MAIN);
    lv_obj_set_style_radius(back, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(back, settings_back_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, ICON_BACK);
    lv_obj_set_style_text_font(back_icon, &lv_font_control_icons_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * heading = lv_label_create(settings_screen);
    lv_label_set_text(heading, "Ustawienia");
    lv_obj_set_pos(heading, 76, 22);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_make(0xE4, 0xE2, 0xE9), LV_PART_MAIN);

    lv_obj_t * list = lv_obj_create(settings_screen);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    int y = 8;
    std::string ip = get_wlan0_ip();
    std::string wifi_status = ip == "127.0.0.1" ? "Brak połączenia" : "Połączono - " + ip;
    std::string ha_status = config_exists() ? "Skonfigurowano" : "Wymaga konfiguracji";

    add_settings_section(list, "ŁĄCZNOŚĆ", &y);
    add_settings_card(list, &y, ICON_WIFI, lv_color_make(0x18, 0x65, 0xA8),
                      "Wi-Fi", wifi_status.c_str(), SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_BLUETOOTH, lv_color_make(0x4F, 0x5D, 0xB8),
                      "Bluetooth Proxy", "Planowane", SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_PLUG, lv_color_make(0x38, 0x6A, 0x20),
                      "Zigbee Router", "Planowane", SETTINGS_ACTION_NONE);

    add_settings_section(list, "PANEL", &y);
    add_settings_card(list, &y, ICON_BRIGHTNESS, lv_color_make(0x9A, 0x56, 0x00),
                      "Ekran", "Jasność i wygaszanie", SETTINGS_ACTION_CONTROLS);
    add_settings_card(list, &y, ICON_VOLUME, lv_color_make(0x7A, 0x48, 0x92),
                      "Dźwięk", "Głośność i mikrofon", SETTINGS_ACTION_CONTROLS);
    add_settings_card(list, &y, ICON_PALETTE, lv_color_make(0x8C, 0x43, 0x53),
                      "Wygląd", "Motyw i ekran główny - planowane", SETTINGS_ACTION_NONE);

    add_settings_section(list, "HOME ASSISTANT", &y);
    add_settings_card(list, &y, ICON_HOME, lv_color_make(0x03, 0x78, 0xA6),
                      "Połączenie", ha_status.c_str(), SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_GLOBE, lv_color_make(0x00, 0x68, 0x74),
                      "Portal WWW", "Planowane", SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_MIC, lv_color_make(0x6B, 0x57, 0x8A),
                      "Asystent głosowy", "HA Assist / Wyoming - planowane", SETTINGS_ACTION_NONE);

    add_settings_section(list, "SYSTEM", &y);
    add_settings_card(list, &y, ICON_DOWNLOAD, lv_color_make(0x38, 0x6A, 0x20),
                      "Aktualizacje", CURRENT_VERSION, SETTINGS_ACTION_INFO);
    add_settings_card(list, &y, ICON_TOOLS, lv_color_make(0x5C, 0x60, 0x6A),
                      "Diagnostyka", "Stan urządzenia", SETTINGS_ACTION_DIAGNOSTICS);
    add_settings_card(list, &y, ICON_INFO, lv_color_make(0x18, 0x65, 0xA8),
                      "Informacje", "Panel TPP01-Z", SETTINGS_ACTION_INFO);

    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, y);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
}

static void create_settings_screen_async(void * user_data) {
    (void)user_data;
    create_settings_screen();
}

static int read_int_file(const char * path, int fallback) {
    std::ifstream file(path);
    int value = fallback;
    if (file.is_open()) file >> value;
    return value;
}

static void set_percent_label(lv_obj_t * label, int value) {
    char text[16];
    snprintf(text, sizeof(text), "%d%%", value);
    lv_label_set_text(label, text);
}

static void brightness_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int percent = lv_slider_get_value(slider);
    set_percent_label(brightness_value_label, percent);

#ifndef PC_SIMULATOR
    std::ofstream brightness("/sys/class/backlight/backlight/brightness");
    if (brightness.is_open()) brightness << (percent * backlight_max / 100);
#endif
}

static void apply_volume(int percent) {
#ifndef PC_SIMULATOR
    if (percent == 0) {
        system("amixer -q -c 0 cset numid=43 off");
    } else {
        // The OEM app also keeps DAC gain fixed and applies 0-100% in software playback.
        system("amixer -q -c 0 cset numid=43 on; "
               "amixer -q -c 0 cset numid=34 2; "
               "amixer -q -c 0 cset numid=35 2");
    }
#else
    (void)percent;
#endif
}

static void volume_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int percent = lv_slider_get_value(slider);
    set_percent_label(volume_value_label, percent);

    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;

    std::ofstream saved_volume("/tuya/data/ha_volume");
    if (saved_volume.is_open()) saved_volume << percent;
    apply_volume(percent);
}

static void control_center_anim_y(void * obj, int32_t y) {
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void snap_control_center(void) {
    int current_y = lv_obj_get_y(control_center);
    int target_y;

    if (control_center_drag_velocity > 700) target_y = 0;
    else if (control_center_drag_velocity < -700) target_y = -480;
    else target_y = current_y >= -240 ? 0 : -480;

    int distance = abs(target_y - current_y);
    if (distance == 0) return;

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, control_center);
    lv_anim_set_exec_cb(&animation, control_center_anim_y);
    lv_anim_set_values(&animation, current_y, target_y);
    lv_anim_set_time(&animation, 120 + distance * 180 / 480);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void control_center_drag_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        control_center_drag_start_y = point.y;
        control_center_drag_start_panel_y = lv_obj_get_y(control_center);
        control_center_drag_last_y = point.y;
        control_center_drag_last_time = custom_tick_get();
        control_center_drag_velocity = 0;
        control_center_drag_active = true;
        lv_anim_del(control_center, control_center_anim_y);
        lv_obj_move_foreground(control_center);
    } else if (code == LV_EVENT_PRESSING && control_center_drag_active) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        uint32_t now = custom_tick_get();
        int delta_y = point.y - control_center_drag_last_y;
        uint32_t delta_time = now - control_center_drag_last_time;
        if (delta_y != 0 && delta_time > 0) {
            control_center_drag_velocity = delta_y * 1000 / (int)delta_time;
            control_center_drag_last_y = point.y;
            control_center_drag_last_time = now;
        }
        int y = control_center_drag_start_panel_y + point.y - control_center_drag_start_y;
        if (y < -480) y = -480;
        if (y > 0) y = 0;
        if (y < -472) y = -480;
        if (y > -8) y = 0;
        lv_obj_set_y(control_center, y);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        control_center_drag_active = false;
        snap_control_center();
    }
}

static void settings_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_set_y(control_center, -480);
    lv_async_call(create_settings_screen_async, NULL);
}

static lv_obj_t * create_control_slider(lv_obj_t * parent, const char * icon_symbol,
                                        int y, int min_value, int value, lv_event_cb_t callback,
                                        lv_obj_t ** value_label) {
    lv_obj_t * icon = lv_label_create(parent);
    lv_label_set_text(icon, icon_symbol);
    lv_obj_set_style_text_font(icon, &lv_font_control_icons_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(icon, lv_color_make(0xD5, 0xD8, 0xE2), LV_PART_MAIN);
    lv_obj_set_pos(icon, 42, y);

    *value_label = lv_label_create(parent);
    set_percent_label(*value_label, value);
    lv_obj_set_style_text_color(*value_label, lv_color_make(0x03, 0xA9, 0xF4), LV_PART_MAIN);
    lv_obj_align(*value_label, LV_ALIGN_TOP_RIGHT, -42, y);

    lv_obj_t * slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 396, 20);
    lv_obj_set_pos(slider, 42, y + 42);
    lv_slider_set_range(slider, min_value, 100);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_make(0x4A, 0x4E, 0x59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_make(0x03, 0xA9, 0xF4), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 2, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, callback, LV_EVENT_RELEASED, NULL);
    return slider;
}

static void create_control_center(lv_obj_t * scr) {
    control_center = lv_obj_create(scr);
    lv_obj_set_size(control_center, 480, 480);
    lv_obj_set_pos(control_center, 0, -480);
    lv_obj_set_style_bg_color(control_center, lv_color_make(0x24, 0x27, 0x30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(control_center, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(control_center, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(control_center, 0, LV_PART_MAIN);
    lv_obj_clear_flag(control_center, LV_OBJ_FLAG_SCROLLABLE);

    backlight_max = read_int_file("/sys/class/backlight/backlight/max_brightness", 255);
    if (backlight_max < 1) backlight_max = 255;
    int brightness_raw = read_int_file("/sys/class/backlight/backlight/brightness", backlight_max * 4 / 5);
    int brightness = brightness_raw * 100 / backlight_max;
    if (brightness < 5) brightness = 5;
    if (brightness > 100) brightness = 100;
#ifndef PC_SIMULATOR
    if (brightness_raw != brightness * backlight_max / 100) {
        std::ofstream brightness_file("/sys/class/backlight/backlight/brightness");
        if (brightness_file.is_open()) brightness_file << (brightness * backlight_max / 100);
    }
#endif
    create_control_slider(control_center, ICON_BRIGHTNESS, 88, 5, brightness,
                          brightness_event_cb, &brightness_value_label);

    int volume = read_int_file("/tuya/data/ha_volume", 80);
    if (volume < 0 || volume > 100) volume = 80;
    create_control_slider(control_center, ICON_VOLUME, 202, 0, volume,
                          volume_event_cb, &volume_value_label);
    apply_volume(volume);

    lv_obj_t * settings = lv_btn_create(control_center);
    lv_obj_set_size(settings, 76, 76);
    lv_obj_align(settings, LV_ALIGN_TOP_MID, 0, 322);
    lv_obj_set_style_bg_color(settings, lv_color_make(0x36, 0x3A, 0x45), LV_PART_MAIN);
    lv_obj_set_style_radius(settings, 38, LV_PART_MAIN);
    lv_obj_add_event_cb(settings, settings_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * settings_icon = lv_label_create(settings);
    lv_label_set_text(settings_icon, ICON_SETTINGS);
    lv_obj_set_style_text_font(settings_icon, &lv_font_control_icons_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(settings_icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(settings_icon, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * handle_zone = lv_obj_create(control_center);
    lv_obj_set_size(handle_zone, 480, 64);
    lv_obj_align(handle_zone, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(handle_zone, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(handle_zone, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(handle_zone, 0, LV_PART_MAIN);
    lv_obj_clear_flag(handle_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(handle_zone, control_center_drag_event_cb, LV_EVENT_ALL, NULL);

    lv_obj_t * handle = lv_obj_create(handle_zone);
    lv_obj_set_size(handle, 72, 8);
    lv_obj_align(handle, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(handle, lv_color_make(0x79, 0x7E, 0x89), LV_PART_MAIN);
    lv_obj_set_style_border_width(handle, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(handle, 4, LV_PART_MAIN);
    lv_obj_clear_flag(handle, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t * edge = lv_obj_create(scr);
    lv_obj_set_size(edge, 480, 30);
    lv_obj_align(edge, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(edge, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(edge, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(edge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(edge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(edge, control_center_drag_event_cb, LV_EVENT_ALL, NULL);
}

// Create the active HA dashboard UI
void create_home_assistant_ui(void) {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_clean(scr); // Clear onboarding elements
    lv_obj_set_style_bg_color(scr, lv_color_make(0x1F, 0x1F, 0x1F), LV_PART_MAIN); // Dark grey

    // 1. Draw Native Home Assistant Logo
    lv_obj_t * img = lv_img_create(scr);
    lv_img_set_src(img, &ha_logo);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 35); // Beautiful centered logo at top

    // 2. Info Button "?" at Top-Right for System Settings & OTA
    lv_obj_t * info_btn = lv_btn_create(scr);
    lv_obj_set_size(info_btn, 45, 45);
    lv_obj_align(info_btn, LV_ALIGN_TOP_RIGHT, -40, 40); // Restored to top-right on square screen
    lv_obj_add_event_cb(info_btn, info_btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_bg_color(info_btn, lv_color_make(0x3B, 0x3B, 0x3B), LV_PART_MAIN);
    lv_obj_set_style_radius(info_btn, 22, LV_PART_MAIN); // Fully round "?" button

    lv_obj_t * info_label = lv_label_create(info_btn);
    lv_label_set_text(info_label, "?");
    lv_obj_set_style_text_color(info_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 0);

    // 3. Create Subtitle showing server URL (placed below logo)
    lv_obj_t * subtitle = lv_label_create(scr);
    std::string clean_url = ha_url;
    if (clean_url.length() > 24) clean_url = clean_url.substr(0, 22) + "..";
    lv_label_set_text(subtitle, clean_url.c_str());
    lv_obj_set_style_text_color(subtitle, lv_color_make(0xAA, 0xAA, 0xAA), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 165);

    // 4. Create central Button: "Lampa Sufitowa"
    lv_obj_t * btn1 = lv_btn_create(scr);
    lv_obj_set_size(btn1, 220, 75);
    lv_obj_align(btn1, LV_ALIGN_CENTER, 0, 45);
    lv_obj_add_event_cb(btn1, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(btn1, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn1, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN); // Initial OFF (Red)
    lv_obj_set_style_bg_color(btn1, lv_color_make(0x00, 0xAA, 0x00), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(btn1, 37, LV_PART_MAIN);

    lv_obj_t * label1 = lv_label_create(btn1);
    lv_label_set_text(label1, "ŻARÓWKA / SUFIT");
    lv_obj_set_style_text_color(label1, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);

    // 5. Create minor Button: "Wentylator"
    lv_obj_t * btn2 = lv_btn_create(scr);
    lv_obj_set_size(btn2, 220, 55);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 0, -45);
    lv_obj_add_event_cb(btn2, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn2, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn2, lv_color_make(0x00, 0xAA, 0x00), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(btn2, 27, LV_PART_MAIN);

    lv_obj_t * label2 = lv_label_create(btn2);
    lv_label_set_text(label2, "WENTYLATOR / NAWIEW");
    lv_obj_set_style_text_color(label2, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, 0);

    create_control_center(scr);
}

// Create the onboarding UI with native QR code
void create_onboarding_ui(const std::string &ip) {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_make(0x1a, 0x1a, 0x1a), LV_PART_MAIN); // Black/Dark

    // 1. Title
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "KONFIGURACJA");
    lv_obj_set_style_text_color(title, lv_color_make(255, 152, 0), LV_PART_MAIN); // Orange
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // 2. Info Button "?" at Top-Right for System Settings & OTA on Onboarding screen
    lv_obj_t * info_btn = lv_btn_create(scr);
    lv_obj_set_size(info_btn, 45, 45);
    lv_obj_align(info_btn, LV_ALIGN_TOP_RIGHT, -40, 40); // Restored to top-right on square screen
    lv_obj_add_event_cb(info_btn, info_btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_bg_color(info_btn, lv_color_make(0x3B, 0x3B, 0x3B), LV_PART_MAIN);
    lv_obj_set_style_radius(info_btn, 22, LV_PART_MAIN);

    lv_obj_t * info_label = lv_label_create(info_btn);
    lv_label_set_text(info_label, "?");
    lv_obj_set_style_text_color(info_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 0);

    // 3. Description
    lv_obj_t * subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Zeskanuj kod, aby skonfigurować");
    lv_obj_set_style_text_color(subtitle, lv_color_make(0xAA, 0xAA, 0xAA), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 65);

    // 4. Generate native QR Code pointing to the web page
    std::string url = "http://" + ip + "/config.html";
    
    lv_obj_t * qr = lv_qrcode_create(scr, 180, lv_color_make(0, 0, 0), lv_color_make(255, 255, 255));
    lv_qrcode_update(qr, url.c_str(), url.length());
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 20);

    // 5. Show raw URL / IP text at the bottom
    lv_obj_t * footer = lv_label_create(scr);
    lv_label_set_text(footer, url.c_str());
    lv_obj_set_style_text_color(footer, lv_color_make(255, 152, 0), LV_PART_MAIN);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -50);

    create_control_center(scr);
}

// Periodical timer check to see if config file has been saved
static void config_poll_timer(lv_timer_t * timer) {
    if (config_exists() && load_configuration()) {
        printf("[Onboarding] Configuration file successfully saved and loaded!\n");
        
        // 1. Kill web server
        system("killall -9 httpd 2>/dev/null");
        
        // 2. Delete timer and load main UI
        lv_timer_del(timer);
        onboarding_active = false;
        close_settings();
        create_home_assistant_ui();
    }
}

int main(void) {
    printf("[HA Panel] Initializing Native LVGL Application...\n");
    
    // Clean old backup binary on start if it exists to release disk space
    unlink("/tuya/data/ha_panel.old");
    
    // 1. Initialize LVGL engine
    lv_init();

    // 2. Initialize display and touch drivers
    if (!hal_display_init()) {
        return 1;
    }
    if (!hal_touch_init()) {
        hal_shutdown();
        return 1;
    }

    // 3. Check for existing Home Assistant credentials
    if (!config_exists()) {
        printf("[Onboarding] No configuration file found. Starting Smart Onboarding...\n");
        onboarding_active = true;
        
        // Get panel's current IP address
        std::string ip = get_wlan0_ip();
        printf("[Onboarding] Panel IP: %s\n", ip.c_str());
        
        // Prepare CGI script permissions and launch HTTP server on port 80
        system("chmod +x /tuya/data/www/cgi-bin/save_config.sh 2>/dev/null");
        system("httpd -h /tuya/data/www -p 80 &");
        
        // Show QR code onboarding UI
        create_onboarding_ui(ip);
        
        // Register 1-second interval timer to check for config
        lv_timer_create(config_poll_timer, 1000, NULL);
    } else {
        printf("[Onboarding] Configuration file exists. Loading active dashboard...\n");
        if (load_configuration()) {
            create_home_assistant_ui();
        } else {
            // Bad config file formatting, force onboarding
            unlink("/tuya/data/ha_config.json");
            printf("[Onboarding] Error reading config file. Redirecting to Onboarding...\n");
            char *args[] = {(char *)"/tuya/data/ha_panel", NULL};
            execv(args[0], args);
        }
    }

    printf("[HA Panel] Main loop running. Rendering UI...\n");

    // 4. Main Event Processing loop
    while (1) {
        lv_timer_handler();
        usleep(5000); // 5ms sleep
    }

    hal_shutdown();
    return 0;
}
