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
const char * CURRENT_VERSION = "v1.2.0";

static lv_obj_t * control_center = NULL;
static lv_obj_t * brightness_value_label = NULL;
static lv_obj_t * volume_value_label = NULL;
static bool control_center_open = false;
static int control_center_swipe_start_y = 0;
static bool control_center_swipe_active = false;
static int backlight_max = 255;

// Declare external native image data
extern const lv_img_dsc_t ha_logo;

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

static void show_info_popup_async(void * user_data) {
    (void)user_data;
    show_info_popup();
}

// Event callback for the Info button "?"
static void info_btn_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) show_info_popup();
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
        int gain = percent / 25 - 1;
        std::stringstream command;
        command << "amixer -q -c 0 cset numid=43 on; "
                << "amixer -q -c 0 cset numid=34 " << gain << "; "
                << "amixer -q -c 0 cset numid=35 " << gain;
        system(command.str().c_str());
    }
#else
    (void)percent;
#endif
}

static int normalize_volume(int percent) {
    if (percent <= 12) return 0;
    int normalized = ((percent + 12) / 25) * 25;
    return normalized > 100 ? 100 : normalized;
}

static void volume_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int percent = lv_slider_get_value(slider);
    set_percent_label(volume_value_label, percent);

    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_RELEASED && code != LV_EVENT_PRESS_LOST) return;

    percent = normalize_volume(percent);
    lv_slider_set_value(slider, percent, LV_ANIM_ON);
    set_percent_label(volume_value_label, percent);

    std::ofstream saved_volume("/tuya/data/ha_volume");
    if (saved_volume.is_open()) saved_volume << percent;
    apply_volume(percent);
}

static void control_center_anim_y(void * obj, int32_t y) {
    lv_obj_set_y((lv_obj_t *)obj, y);
}

static void set_control_center_open(bool open) {
    if (!control_center || control_center_open == open) return;
    control_center_open = open;
    lv_obj_move_foreground(control_center);

    lv_anim_t animation;
    lv_anim_init(&animation);
    lv_anim_set_var(&animation, control_center);
    lv_anim_set_exec_cb(&animation, control_center_anim_y);
    lv_anim_set_values(&animation, lv_obj_get_y(control_center), open ? 0 : -390);
    lv_anim_set_time(&animation, 260);
    lv_anim_set_path_cb(&animation, lv_anim_path_ease_out);
    lv_anim_start(&animation);
}

static void top_edge_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_point_t point;

    if (code == LV_EVENT_PRESSED) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        control_center_swipe_start_y = point.y;
        control_center_swipe_active = true;
    } else if (code == LV_EVENT_PRESSING && control_center_swipe_active) {
        lv_indev_get_point(lv_indev_get_act(), &point);
        if (point.y - control_center_swipe_start_y < 60) return;
        control_center_swipe_active = false;
        set_control_center_open(true);
        lv_indev_wait_release(lv_indev_get_act());
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        control_center_swipe_active = false;
    }
}

static void control_center_event_cb(lv_event_t * e) {
    if (!control_center_open) return;
    if (lv_event_get_code(e) == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_get_act()) == LV_DIR_TOP) {
        set_control_center_open(false);
        lv_indev_wait_release(lv_indev_get_act());
    }
}

static void close_control_center_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) set_control_center_open(false);
}

static void settings_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    set_control_center_open(false);
    lv_async_call(show_info_popup_async, NULL);
}

static lv_obj_t * create_control_slider(lv_obj_t * parent, const char * title,
                                        int y, int min_value, int value, lv_event_cb_t callback,
                                        lv_obj_t ** value_label) {
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_color(label, lv_color_make(0xD5, 0xD8, 0xE2), LV_PART_MAIN);
    lv_obj_set_pos(label, 42, y);

    *value_label = lv_label_create(parent);
    set_percent_label(*value_label, value);
    lv_obj_set_style_text_color(*value_label, lv_color_make(0x03, 0xA9, 0xF4), LV_PART_MAIN);
    lv_obj_align(*value_label, LV_ALIGN_TOP_RIGHT, -42, y);

    lv_obj_t * slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 396, 20);
    lv_obj_set_pos(slider, 42, y + 32);
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
    control_center_open = false;

    lv_obj_t * edge = lv_obj_create(scr);
    lv_obj_set_size(edge, 480, 30);
    lv_obj_align(edge, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_opa(edge, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(edge, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(edge, 0, LV_PART_MAIN);
    lv_obj_clear_flag(edge, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(edge, top_edge_event_cb, LV_EVENT_ALL, NULL);

    control_center = lv_obj_create(scr);
    lv_obj_set_size(control_center, 480, 390);
    lv_obj_set_pos(control_center, 0, -390);
    lv_obj_set_style_bg_color(control_center, lv_color_make(0x24, 0x27, 0x30), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(control_center, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(control_center, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(control_center, 28, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(control_center, 35, LV_PART_MAIN);
    lv_obj_set_style_shadow_opa(control_center, LV_OPA_50, LV_PART_MAIN);
    lv_obj_clear_flag(control_center, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_event_cb(scr, control_center_event_cb);
    lv_obj_add_event_cb(scr, control_center_event_cb, LV_EVENT_GESTURE, NULL);

    lv_obj_t * title = lv_label_create(control_center);
    lv_label_set_text(title, "CENTRUM STEROWANIA");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 26, 18);

    lv_obj_t * version = lv_label_create(control_center);
    lv_label_set_text(version, CURRENT_VERSION);
    lv_obj_set_style_text_color(version, lv_color_make(0x7F, 0x85, 0x93), LV_PART_MAIN);
    lv_obj_align(version, LV_ALIGN_TOP_RIGHT, -26, 18);

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
    create_control_slider(control_center, "JASNOŚĆ", 72, 5, brightness,
                          brightness_event_cb, &brightness_value_label);

    int volume = normalize_volume(read_int_file("/tuya/data/ha_volume", 50));
    create_control_slider(control_center, "GŁOŚNOŚĆ", 160, 0, volume,
                          volume_event_cb, &volume_value_label);
    apply_volume(volume);

    lv_obj_t * settings = lv_btn_create(control_center);
    lv_obj_set_size(settings, 396, 58);
    lv_obj_set_pos(settings, 42, 256);
    lv_obj_set_style_bg_color(settings, lv_color_make(0x36, 0x3A, 0x45), LV_PART_MAIN);
    lv_obj_set_style_radius(settings, 18, LV_PART_MAIN);
    lv_obj_add_event_cb(settings, settings_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * settings_label = lv_label_create(settings);
    lv_label_set_text(settings_label, "USTAWIENIA");
    lv_obj_set_style_text_color(settings_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(settings_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * handle = lv_btn_create(control_center);
    lv_obj_set_size(handle, 72, 22);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(handle, lv_color_make(0x69, 0x6E, 0x7A), LV_PART_MAIN);
    lv_obj_set_style_radius(handle, 11, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(handle, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(handle, close_control_center_cb, LV_EVENT_CLICKED, NULL);
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
