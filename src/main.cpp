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

// Create the active HA dashboard UI
void create_home_assistant_ui(void) {
    lv_obj_t * scr = lv_scr_act();
    lv_obj_clean(scr); // Clear onboarding elements
    lv_obj_set_style_bg_color(scr, lv_color_make(0x1F, 0x1F, 0x1F), LV_PART_MAIN); // Dark grey

    // 1. Draw Native Home Assistant Logo
    lv_obj_t * img = lv_img_create(scr);
    lv_img_set_src(img, &ha_logo);
    lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 35); // Beautiful centered logo at top

    // 2. Create Subtitle showing server URL (placed below logo)
    lv_obj_t * subtitle = lv_label_create(scr);
    std::string clean_url = ha_url;
    if (clean_url.length() > 24) clean_url = clean_url.substr(0, 22) + "..";
    lv_label_set_text(subtitle, clean_url.c_str());
    lv_obj_set_style_text_color(subtitle, lv_color_make(0xAA, 0xAA, 0xAA), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 165);

    // 3. Create central Button: "Lampa Sufitowa"
    lv_obj_t * btn1 = lv_btn_create(scr);
    lv_obj_set_size(btn1, 220, 75);
    lv_obj_align(btn1, LV_ALIGN_CENTER, 0, 45); // shifted down to accommodate logo
    lv_obj_add_event_cb(btn1, btn_event_cb, LV_EVENT_ALL, NULL);
    lv_obj_set_style_bg_color(btn1, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN); // Initial OFF (Red)
    lv_obj_set_style_radius(btn1, 37, LV_PART_MAIN);

    lv_obj_t * label1 = lv_label_create(btn1);
    lv_label_set_text(label1, "BULB / SUFIT");
    lv_obj_set_style_text_color(label1, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);

    // 4. Create minor Button: "Wentylator"
    lv_obj_t * btn2 = lv_btn_create(scr);
    lv_obj_set_size(btn2, 220, 55);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 0, -45); // shifted down
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
    lv_label_set_text(title, "🏡 ONBOARDING");
    lv_obj_set_style_text_color(title, lv_color_make(255, 152, 0), LV_PART_MAIN); // Orange
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 35);

    // 2. Description
    lv_obj_t * subtitle = lv_label_create(scr);
    lv_label_set_text(subtitle, "Skanuj telefonem, aby skonfigurowac:");
    lv_obj_set_style_text_color(subtitle, lv_color_make(0xAA, 0xAA, 0xAA), LV_PART_MAIN);
    lv_obj_align(subtitle, LV_ALIGN_TOP_MID, 0, 65);

    // 3. Generate native QR Code pointing to the web page
    // Format: http://<PANEL_IP>/config.html
    std::string url = "http://" + ip + "/config.html";
    
    // Create QR code widget (size 180x180)
    lv_obj_t * qr = lv_qrcode_create(scr, 180, lv_color_make(0, 0, 0), lv_color_make(255, 255, 255));
    lv_qrcode_update(qr, url.c_str(), url.length());
    lv_obj_align(qr, LV_ALIGN_CENTER, 0, 20);

    // 4. Show raw URL / IP text at the bottom
    lv_obj_t * footer = lv_label_create(scr);
    lv_label_set_text(footer, url.c_str());
    lv_obj_set_style_text_color(footer, lv_color_make(255, 152, 0), LV_PART_MAIN);
    lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -50);
}

// Periodical timer check to see if config file has been saved
static void config_poll_timer(lv_timer_t * timer) {
    if (config_exists()) {
        printf("[Onboarding] Configuration file found! Loading dashboard...\n");
        
        // 1. Kill web server
        system("killall -9 httpd 2>/dev/null");
        
        // 2. Set config variables
        ha_url = "http://localhost:8123"; // fallback or we parse file
        
        // 3. Delete timer and load main UI
        lv_timer_del(timer);
        onboarding_active = false;
        create_home_assistant_ui();
    }
}

int main(void) {
    printf("[HA Panel] Initializing Native LVGL Application...\n");
    
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
        // Simulated loading (in real app, parse /tuya/data/ha_config.json)
        ha_url = "http://192.168.1.100:8123";
        create_home_assistant_ui();
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
