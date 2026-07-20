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
std::string github_token = "";
bool onboarding_active = false;

// Version of current binary
const char * CURRENT_VERSION = "v1.0.0";

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
    github_token = parse_json_value(json, "github_token");
    
    return !ha_url.empty();
}

// Perform OTA self-replacement update directly from GitHub Releases API
bool perform_github_ota() {
    printf("[OTA] Starting OTA process...\n");
    if (github_token.empty()) {
        printf("[OTA] Error: GitHub token is not configured. OTA aborted.\n");
        return false;
    }
    
    // 1. Fetch latest release info via secure wget command
    // We send public/private repo query with Authorization Bearer token header
    std::string cmd_fetch = "wget -qO- --header=\"Authorization: Bearer " + github_token + "\" "
                            "--header=\"User-Agent: Mozilla/5.0\" "
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
    
    // 2. Download the binary asset to temporary persistent path
    printf("[OTA] Downloading new binary asset from %s...\n", download_url.c_str());
    std::string cmd_download = "wget -q --header=\"Authorization: Bearer " + github_token + "\" "
                               "--header=\"User-Agent: Mozilla/5.0\" "
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
        static bool state = false;
        state = !state;
        
        if (state) {
            lv_obj_set_style_bg_color(btn, lv_color_make(0x00, 0xAA, 0x00), LV_PART_MAIN); // Green for ON
            printf("[HA Click] Light turned ON (REST/Websocket request simulation to %s)!\n", ha_url.c_str());
        } else {
            lv_obj_set_style_bg_color(btn, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN); // Red for OFF
            printf("[HA Click] Light turned OFF (REST/Websocket request simulation to %s)!\n", ha_url.c_str());
        }
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
        lv_obj_t * mbox = lv_msgbox_create(NULL, "AKTUALIZACJA", "Pobieranie nowej wersji z GitHub...", NULL, false);
        lv_obj_align(mbox, LV_ALIGN_CENTER, 0, 0);
        lv_timer_handler(); // Redraw immediately
        
        // Execute background OTA
        if (!perform_github_ota()) {
            lv_msgbox_close(mbox);
            lv_msgbox_create(NULL, "BLAD", "Aktualizacja nie powiodla sie!\nSprawdz polaczenie lub Token.", NULL, true);
        }
    } else { // "ZAMKNIJ" (Close)
        lv_msgbox_close(obj);
    }
}

// Event callback for the Info button "?"
static void info_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        // Create a beautiful system information modal window
        static const char * btns[] = {"AKTUALIZUJ", "ZAMKNIJ", ""};
        
        std::string ip = get_wlan0_ip();
        std::stringstream ss;
        ss << "WERSJA: " << CURRENT_VERSION << "\nIP: " << ip;
        if (github_token.empty()) {
            ss << "\n(Brak Tokenu GitHub)";
        } else {
            ss << "\n(Token GitHub aktywny)";
        }
        
        lv_obj_t * mbox = lv_msgbox_create(NULL, "INFO SYSTEM", ss.str().c_str(), btns, false);
        lv_obj_add_event_cb(mbox, ota_msgbox_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_align(mbox, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_color(mbox, lv_color_make(0x2D, 0x2D, 0x2D), LV_PART_MAIN);
    }
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
    lv_obj_align(info_btn, LV_ALIGN_TOP_MID, 110, 50); // Positioned inside the circular screen boundaries
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
    lv_obj_set_style_bg_color(btn1, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN); // Initial OFF (Red)
    lv_obj_set_style_radius(btn1, 37, LV_PART_MAIN);

    lv_obj_t * label1 = lv_label_create(btn1);
    lv_label_set_text(label1, "BULB / SUFIT");
    lv_obj_set_style_text_color(label1, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);

    // 5. Create minor Button: "Wentylator"
    lv_obj_t * btn2 = lv_btn_create(scr);
    lv_obj_set_size(btn2, 220, 55);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 0, -45);
    lv_obj_add_event_cb(btn2, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_bg_color(btn2, lv_color_make(0x3B, 0x3B, 0x3B), LV_PART_MAIN);
    lv_obj_set_style_radius(btn2, 27, LV_PART_MAIN);

    lv_obj_t * label2 = lv_label_create(btn2);
    lv_label_set_text(label2, "FAN / NAWIEW");
    lv_obj_set_style_text_color(label2, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, 0);
}

// Create the onboarding UI with native QR code
void create_onboarding_ui(const std::string &ip) {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_make(0x1a, 0x1a, 0x1a), LV_PART_MAIN); // Black/Dark

    // 1. Title
    lv_obj_t * title = lv_label_create(scr);
    lv_label_set_text(title, "ONBOARDING");
    lv_obj_set_style_text_color(title, lv_color_make(255, 152, 0), LV_PART_MAIN); // Orange
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // 2. Info Button "?" at Top-Right for System Settings & OTA on Onboarding screen
    lv_obj_t * info_btn = lv_btn_create(scr);
    lv_obj_set_size(info_btn, 45, 45);
    lv_obj_align(info_btn, LV_ALIGN_TOP_MID, 110, 50); // Positioned inside the circular screen boundaries
    lv_obj_add_event_cb(info_btn, info_btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_bg_color(info_btn, lv_color_make(0x3B, 0x3B, 0x3B), LV_PART_MAIN);
    lv_obj_set_style_radius(info_btn, 22, LV_PART_MAIN);

    lv_obj_t * info_label = lv_label_create(info_btn);
    lv_label_set_text(info_label, "?");
    lv_obj_set_style_text_color(info_label, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(info_label, LV_ALIGN_CENTER, 0, 0);

    // 3. Description
    lv_obj_t * subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Skanuj telefonem, aby skonfigurowac:");
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
