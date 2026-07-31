#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <signal.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <dirent.h>
#include <string>
#include <map>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <chrono>
#include <atomic>
#include <ctype.h>
#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "../lvgl/lvgl.h"
#include "../lvgl/src/extra/libs/qrcode/lv_qrcode.h"
#include "mqtt_client.h"

// HAL declarations
bool hal_display_init(void);
bool hal_touch_init(void);
void hal_shutdown(void);

extern bool g_screen_blanked;
extern int g_active_backlight_raw;
extern uint32_t g_last_wake_time;
extern void hal_set_backlight(int raw_val);
extern void hal_wake_screen(void);
extern void hal_blank_screen(void);
extern "C" uint32_t custom_tick_get(void);

MqttClient g_mqtt_client;
MqttConfig g_mqtt_config;
bool g_web_autostart = true;
bool g_ha_autostart = false;
bool g_cgi_mode = false;

static uint32_t screen_timeout_ms = 30000; // 30 seconds default screen timeout
static bool auto_brightness_enabled = false;
static int manual_brightness_percent = 80;
static int auto_brightness_percent = 80;
static float filtered_ambient_lux = -1.0f;
static std::string ambient_raw_path;
static std::string ambient_scale_path;

static void screensaver_timer_cb(lv_timer_t * timer) {
    (void)timer;
    if (screen_timeout_ms == 0) return; // 0 = Always ON

    if (!g_screen_blanked) {
        uint32_t now = custom_tick_get();
        if (g_last_wake_time > 0 && (now - g_last_wake_time < 5000)) {
            lv_disp_trig_activity(NULL);
            return;
        }

        uint32_t inactive = lv_disp_get_inactive_time(NULL);
        if (inactive >= screen_timeout_ms) {
            printf("[ScreenSaver %u ms] Inactivity timeout (%u >= %u ms). Blanking screen!\n", now, inactive, screen_timeout_ms);
            hal_blank_screen();
        }
    }
}

// Global configuration variables
std::string ha_url = "";
std::string ha_token = "";
std::string ha_entity_1 = "";
std::string ha_entity_2 = "";
std::string ha_entity_1_name = "ŚWIATŁO";
std::string ha_entity_2_name = "WENTYLATOR";
bool onboarding_active = false;

// Version of current binary
const char * CURRENT_VERSION = "v1.8.1";

static lv_obj_t * control_center = NULL;
static lv_obj_t * brightness_value_label = NULL;
static lv_obj_t * brightness_slider = NULL;
static lv_obj_t * volume_value_label = NULL;
static int control_center_drag_start_y = 0;
static int control_center_drag_start_panel_y = -480;
static bool control_center_drag_active = false;
static int control_center_drag_last_y = 0;
static uint32_t control_center_drag_last_time = 0;
static int control_center_drag_velocity = 0;
static int backlight_max = 255;
static lv_obj_t * settings_screen = NULL;
static lv_obj_t * updates_screen = NULL;
static lv_obj_t * diagnostics_screen = NULL;
static lv_obj_t * info_screen = NULL;
static lv_obj_t * display_screen = NULL;
static lv_obj_t * wifi_screen = NULL;
static lv_obj_t * wifi_status_dot = NULL;
static lv_obj_t * wifi_status_label = NULL;
static lv_obj_t * wifi_switch = NULL;
static lv_obj_t * wifi_avail_container = NULL;
static lv_obj_t * wifi_scan_btn_label = NULL;
static bool wifi_interface_enabled = true;
static bool wifi_static_ip_mode = false;
static lv_obj_t * display_auto_switch = NULL;
static lv_obj_t * display_auto_status_label = NULL;
static lv_obj_t * display_brightness_slider = NULL;
static lv_obj_t * display_brightness_label = NULL;
static lv_obj_t * display_timeout_buttons[4] = {NULL, NULL, NULL, NULL};
static const int display_timeout_values[4] = {15, 30, 60, 0};

// Declare external native image data
extern const lv_img_dsc_t ha_logo;
LV_FONT_DECLARE(lv_font_control_icons_24);
LV_FONT_DECLARE(lv_font_montserrat_12_pl);
LV_FONT_DECLARE(lv_font_montserrat_14_pl);
LV_FONT_DECLARE(lv_font_montserrat_16_pl);
LV_FONT_DECLARE(lv_font_montserrat_20_pl);
LV_FONT_DECLARE(lv_font_montserrat_24_pl);

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
#define ICON_REFRESH    "\xEF\x80\xA1"

// Helper function to get panel IP address dynamically across all interfaces
std::string get_wlan0_ip() {
    struct ifaddrs *ifaddr, *ifa;
    std::string ip = "127.0.0.1";

    if (getifaddrs(&ifaddr) == 0) {
        // Pass 1: Look specifically for wlan0 with valid IPv4
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            std::string if_name = ifa->ifa_name;
            if (if_name == "wlan0") {
                struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
                if (sa->sin_addr.s_addr != htonl(INADDR_LOOPBACK) && sa->sin_addr.s_addr != 0) {
                    char host[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(sa->sin_addr), host, INET_ADDRSTRLEN);
                    ip = host;
                    freeifaddrs(ifaddr);
                    return ip;
                }
            }
        }

        // Pass 2: Fallback to any non-loopback IPv4 interface
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            std::string if_name = ifa->ifa_name;
            if (if_name != "lo") {
                struct sockaddr_in *sa = (struct sockaddr_in *)ifa->ifa_addr;
                if (sa->sin_addr.s_addr != htonl(INADDR_LOOPBACK) && sa->sin_addr.s_addr != 0) {
                    char host[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &(sa->sin_addr), host, INET_ADDRSTRLEN);
                    ip = host;
                    break;
                }
            }
        }
        freeifaddrs(ifaddr);
    }
    return ip;
}

// Check if config file exists
bool config_exists() {
    return access("/tuya/data/ha_config.json", F_OK) == 0;
}

static void append_utf8(std::string &output, unsigned int codepoint) {
    if (codepoint <= 0x7f) {
        output.push_back((char)codepoint);
    } else if (codepoint <= 0x7ff) {
        output.push_back((char)(0xc0 | (codepoint >> 6)));
        output.push_back((char)(0x80 | (codepoint & 0x3f)));
    } else {
        output.push_back((char)(0xe0 | (codepoint >> 12)));
        output.push_back((char)(0x80 | ((codepoint >> 6) & 0x3f)));
        output.push_back((char)(0x80 | (codepoint & 0x3f)));
    }
}

// Lightweight JSON string parser with escape handling, avoiding a heavy JSON dependency.
std::string parse_json_value(const std::string &json, const std::string &key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return "";
    
    size_t colon_pos = json.find(":", key_pos);
    if (colon_pos == std::string::npos) return "";
    
    size_t start_quote = json.find("\"", colon_pos);
    if (start_quote == std::string::npos) return "";
    
    std::string value;
    for (size_t i = start_quote + 1; i < json.size(); i++) {
        char c = json[i];
        if (c == '"') return value;
        if (c != '\\') {
            value.push_back(c);
            continue;
        }
        if (++i >= json.size()) return "";
        char escaped = json[i];
        switch (escaped) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u': {
                if (i + 4 >= json.size()) return "";
                unsigned int codepoint = 0;
                for (int n = 0; n < 4; n++) {
                    char hex = json[++i];
                    codepoint <<= 4;
                    if (hex >= '0' && hex <= '9') codepoint |= (unsigned int)(hex - '0');
                    else if (hex >= 'a' && hex <= 'f') codepoint |= (unsigned int)(hex - 'a' + 10);
                    else if (hex >= 'A' && hex <= 'F') codepoint |= (unsigned int)(hex - 'A' + 10);
                    else return "";
                }
                append_utf8(value, codepoint);
                break;
            }
            default: return "";
        }
    }
    return "";
}

static std::string json_escape(const std::string &value) {
    std::string escaped;
    escaped.reserve(value.size() + 8);
    const char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < value.size(); i++) {
        unsigned char c = (unsigned char)value[i];
        switch (c) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\b': escaped += "\\b"; break;
            case '\f': escaped += "\\f"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (c < 0x20) {
                    escaped += "\\u00";
                    escaped.push_back(hex[c >> 4]);
                    escaped.push_back(hex[c & 0x0f]);
                } else {
                    escaped.push_back((char)c);
                }
        }
    }
    return escaped;
}

size_t parse_json_int_value(const std::string &json, const std::string &key) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return 0;
    size_t colon_pos = json.find(":", key_pos);
    if (colon_pos == std::string::npos) return 0;
    size_t val_start = json.find_first_of("0123456789", colon_pos);
    if (val_start == std::string::npos) return 0;
    return (size_t)atoll(json.c_str() + val_start);
}

bool parse_json_bool_value(const std::string &json, const std::string &key, bool fallback = false) {
    size_t key_pos = json.find("\"" + key + "\"");
    if (key_pos == std::string::npos) return fallback;
    size_t colon_pos = json.find(":", key_pos);
    if (colon_pos == std::string::npos) return fallback;
    
    size_t true_pos = json.find("true", colon_pos);
    size_t false_pos = json.find("false", colon_pos);
    size_t comma_pos = json.find_first_of(",}\n\r", colon_pos);

    if (true_pos != std::string::npos && (comma_pos == std::string::npos || true_pos < comma_pos)) {
        return true;
    }
    if (false_pos != std::string::npos && (comma_pos == std::string::npos || false_pos < comma_pos)) {
        return false;
    }
    return fallback;
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
    ha_entity_1 = parse_json_value(json, "entity_1");
    ha_entity_2 = parse_json_value(json, "entity_2");
    std::string configured_name_1 = parse_json_value(json, "entity_1_name");
    std::string configured_name_2 = parse_json_value(json, "entity_2_name");

    while (!ha_url.empty() && (ha_url.back() == ' ' || ha_url.back() == '\r' || ha_url.back() == '\n' || ha_url.back() == '\t')) ha_url.pop_back();
    while (!ha_token.empty() && (ha_token.back() == ' ' || ha_token.back() == '\r' || ha_token.back() == '\n' || ha_token.back() == '\t')) ha_token.pop_back();

    if (ha_entity_1.empty()) ha_entity_1 = "light.living_room";
    if (ha_entity_2.empty()) ha_entity_2 = "switch.fan";

    if (!configured_name_1.empty()) ha_entity_1_name = configured_name_1;
    if (!configured_name_2.empty()) ha_entity_2_name = configured_name_2;

    // Load MQTT parameters
    g_mqtt_config.enabled = parse_json_bool_value(json, "mqtt_enabled", false);
    g_mqtt_config.host = parse_json_value(json, "mqtt_host");
    int port_val = (int)parse_json_int_value(json, "mqtt_port");
    g_mqtt_config.port = (port_val > 0) ? port_val : 1883;

    g_mqtt_config.username = parse_json_value(json, "mqtt_user");
    g_mqtt_config.password = parse_json_value(json, "mqtt_pass");

    std::string topic = parse_json_value(json, "mqtt_topic");
    g_mqtt_config.base_topic = !topic.empty() ? topic : "panel/tpp01";

    g_mqtt_config.discovery = parse_json_bool_value(json, "mqtt_discovery", true);

    g_web_autostart = parse_json_bool_value(json, "web_autostart", true);
    g_ha_autostart = parse_json_bool_value(json, "ha_autostart", false);

    if (!g_cgi_mode) {
        printf("[Config] MQTT enabled=%d, host='%s', port=%d, topic='%s', web_autostart=%d\n",
               g_mqtt_config.enabled, g_mqtt_config.host.c_str(), g_mqtt_config.port, g_mqtt_config.base_topic.c_str(), g_web_autostart);
    }

    return !ha_url.empty() && !ha_token.empty();
}

bool save_configuration() {
    std::string temp_path = "/tuya/data/ha_config.json.tmp." + std::to_string((long long)getpid());
    std::ofstream f(temp_path.c_str(), std::ios::out | std::ios::trunc);
    if (!f.is_open()) return false;

    f << "{\n"
      << "  \"ha_url\": \"" << json_escape(ha_url) << "\",\n"
      << "  \"ha_token\": \"" << json_escape(ha_token) << "\",\n"
      << "  \"entity_1\": \"" << json_escape(ha_entity_1) << "\",\n"
      << "  \"entity_1_name\": \"" << json_escape(ha_entity_1_name) << "\",\n"
      << "  \"entity_2\": \"" << json_escape(ha_entity_2) << "\",\n"
      << "  \"entity_2_name\": \"" << json_escape(ha_entity_2_name) << "\",\n"
      << "  \"mqtt_enabled\": " << (g_mqtt_config.enabled ? "true" : "false") << ",\n"
      << "  \"mqtt_host\": \"" << json_escape(g_mqtt_config.host) << "\",\n"
      << "  \"mqtt_port\": " << g_mqtt_config.port << ",\n"
      << "  \"mqtt_user\": \"" << json_escape(g_mqtt_config.username) << "\",\n"
      << "  \"mqtt_pass\": \"" << json_escape(g_mqtt_config.password) << "\",\n"
      << "  \"mqtt_topic\": \"" << json_escape(g_mqtt_config.base_topic) << "\",\n"
      << "  \"mqtt_discovery\": " << (g_mqtt_config.discovery ? "true" : "false") << ",\n"
      << "  \"web_autostart\": " << (g_web_autostart ? "true" : "false") << ",\n"
      << "  \"ha_autostart\": " << (g_ha_autostart ? "true" : "false") << "\n"
      << "}\n";
    f.close();
    if (!f) {
        unlink(temp_path.c_str());
        return false;
    }
    chmod(temp_path.c_str(), 0600);
    if (rename(temp_path.c_str(), "/tuya/data/ha_config.json") != 0) {
        unlink(temp_path.c_str());
        return false;
    }
    return true;
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static std::string url_decode(const std::string &value) {
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); i++) {
        if (value[i] == '+') {
            decoded.push_back(' ');
        } else if (value[i] == '%' && i + 2 < value.size()) {
            int high = hex_value(value[i + 1]);
            int low = hex_value(value[i + 2]);
            if (high >= 0 && low >= 0) {
                decoded.push_back((char)((high << 4) | low));
                i += 2;
            } else {
                decoded.push_back(value[i]);
            }
        } else {
            decoded.push_back(value[i]);
        }
    }
    return decoded;
}

static std::string form_value(const std::string &body, const std::string &key) {
    size_t start = 0;
    while (start <= body.size()) {
        size_t end = body.find('&', start);
        if (end == std::string::npos) end = body.size();
        size_t equals = body.find('=', start);
        if (equals != std::string::npos && equals < end && url_decode(body.substr(start, equals - start)) == key) {
            return url_decode(body.substr(equals + 1, end - equals - 1));
        }
        if (end == body.size()) break;
        start = end + 1;
    }
    return "";
}

static std::string trim_copy(const std::string &value) {
    size_t start = 0;
    size_t end = value.size();
    while (start < end && isspace((unsigned char)value[start])) start++;
    while (end > start && isspace((unsigned char)value[end - 1])) end--;
    return value.substr(start, end - start);
}

static bool form_bool(const std::string &body, const std::string &key) {
    std::string value = form_value(body, key);
    return value == "true" || value == "1" || value == "on";
}

static bool valid_entity_id(const std::string &value) {
    size_t dot = value.find('.');
    if (value.empty() || value.size() > 128 || dot == std::string::npos || dot == 0 || dot == value.size() - 1) return false;
    for (size_t i = 0; i < value.size(); i++) {
        unsigned char c = (unsigned char)value[i];
        if (!(isalnum(c) || c == '_' || c == '.')) return false;
    }
    return true;
}

static void cgi_json_response(int status, const std::string &json) {
    if (status != 200) {
        const char *reason = status == 403 ? "Forbidden" : (status == 500 ? "Internal Server Error" : "Bad Request");
        printf("Status: %d %s\r\n", status, reason);
    }
    printf("Content-Type: application/json; charset=utf-8\r\n");
    printf("Cache-Control: no-store\r\n\r\n");
    printf("%s\n", json.c_str());
}

static int cgi_config_main(void) {
    const char *method = getenv("REQUEST_METHOD");
    if (!method || std::string(method) != "POST") {
        cgi_json_response(400, "{\"ok\":false,\"error\":\"POST required\"}");
        return 1;
    }
    const char *requested_with = getenv("HTTP_X_REQUESTED_WITH");
    if (!requested_with || std::string(requested_with) != "TPP01-Panel") {
        cgi_json_response(403, "{\"ok\":false,\"error\":\"Invalid request origin\"}");
        return 1;
    }

    long content_length = 0;
    const char *length_value = getenv("CONTENT_LENGTH");
    if (length_value) content_length = strtol(length_value, NULL, 10);
    if (content_length <= 0 || content_length > 16384) {
        cgi_json_response(400, "{\"ok\":false,\"error\":\"Invalid request size\"}");
        return 1;
    }

    std::string body((size_t)content_length, '\0');
    if (fread(&body[0], 1, (size_t)content_length, stdin) != (size_t)content_length) {
        cgi_json_response(400, "{\"ok\":false,\"error\":\"Incomplete request\"}");
        return 1;
    }

    int lock_fd = open("/tuya/data/ha_config.lock", O_CREAT | O_RDWR, 0600);
    if (lock_fd < 0 || flock(lock_fd, LOCK_EX) != 0) {
        if (lock_fd >= 0) close(lock_fd);
        cgi_json_response(500, "{\"ok\":false,\"error\":\"Configuration is busy\"}");
        return 1;
    }

    if (config_exists()) load_configuration();
    std::string section = form_value(body, "section");
    std::string error;
    std::string field;

    if (section == "ha") {
        bool disconnect = form_bool(body, "disconnect");
        std::string new_url = trim_copy(form_value(body, "ha_url"));
        std::string new_token = trim_copy(form_value(body, "ha_token"));
        std::string new_entity_1 = trim_copy(form_value(body, "entity_1"));
        std::string new_entity_2 = trim_copy(form_value(body, "entity_2"));
        std::string new_name_1 = trim_copy(form_value(body, "entity_1_name"));
        std::string new_name_2 = trim_copy(form_value(body, "entity_2_name"));

        if (disconnect) {
            ha_url.clear();
            ha_token.clear();
            ha_entity_1.clear();
            ha_entity_2.clear();
        } else if (!(new_url.compare(0, 7, "http://") == 0 || new_url.compare(0, 8, "https://") == 0) || new_url.size() > 512) {
            field = "ha_url"; error = "Invalid Home Assistant URL";
        } else if (new_token.empty() && ha_token.empty()) {
            field = "ha_token"; error = "Access token is required";
        } else if (!valid_entity_id(new_entity_1)) {
            field = "entity_1"; error = "Invalid first entity ID";
        } else if (!valid_entity_id(new_entity_2)) {
            field = "entity_2"; error = "Invalid second entity ID";
        } else if (new_name_1.empty() || new_name_1.size() > 64 || new_name_2.empty() || new_name_2.size() > 64) {
            field = "entity_names"; error = "Entity names must contain 1 to 64 bytes";
        } else {
            ha_url = new_url;
            if (!new_token.empty()) ha_token = new_token;
            ha_entity_1 = new_entity_1;
            ha_entity_1_name = new_name_1;
            ha_entity_2 = new_entity_2;
            ha_entity_2_name = new_name_2;
        }
    } else if (section == "mqtt") {
        std::string host = trim_copy(form_value(body, "mqtt_host"));
        std::string port_text = trim_copy(form_value(body, "mqtt_port"));
        std::string username = form_value(body, "mqtt_user");
        std::string password = form_value(body, "mqtt_pass");
        std::string topic = trim_copy(form_value(body, "mqtt_topic"));
        char *port_end = NULL;
        long port = strtol(port_text.c_str(), &port_end, 10);
        bool enabled = form_bool(body, "mqtt_enabled");

        if (enabled && (host.empty() || host.size() > 255)) {
            field = "mqtt_host"; error = "MQTT host is required";
        } else if (port_text.empty() || !port_end || *port_end != '\0' || port < 1 || port > 65535) {
            field = "mqtt_port"; error = "MQTT port must be between 1 and 65535";
        } else if (username.size() > 255 || password.size() > 512) {
            field = "mqtt_credentials"; error = "MQTT credentials are too long";
        } else if (topic.empty() || topic.size() > 255) {
            field = "mqtt_topic"; error = "MQTT base topic is required";
        } else {
            g_mqtt_config.enabled = enabled;
            g_mqtt_config.host = host;
            g_mqtt_config.port = (int)port;
            g_mqtt_config.username = username;
            if (!password.empty()) g_mqtt_config.password = password;
            g_mqtt_config.base_topic = topic;
            g_mqtt_config.discovery = form_bool(body, "mqtt_discovery");
        }
    } else if (section == "system") {
        g_web_autostart = form_bool(body, "web_autostart");
        g_ha_autostart = form_bool(body, "ha_autostart");
    } else {
        field = "section"; error = "Unknown configuration section";
    }

    bool saved = false;
    if (error.empty()) saved = save_configuration();
    flock(lock_fd, LOCK_UN);
    close(lock_fd);

    if (!error.empty()) {
        cgi_json_response(400, "{\"ok\":false,\"field\":\"" + json_escape(field) + "\",\"error\":\"" + json_escape(error) + "\"}");
        return 1;
    }
    if (!saved) {
        cgi_json_response(500, "{\"ok\":false,\"error\":\"Unable to save configuration\"}");
        return 1;
    }

    cgi_json_response(200, "{\"ok\":true,\"section\":\"" + json_escape(section) + "\",\"restart_required\":true}");
    return 0;
}

static std::string read_text_file(const char *path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) return "";
    std::stringstream contents;
    contents << file.rdbuf();
    return contents.str();
}

static int cgi_status_main(void) {
    if (config_exists()) load_configuration();

    std::string logs = read_text_file("/tmp/ha_panel.log");
    size_t ha_ok = logs.rfind("AUTH OK");
    size_t ha_closed = logs.rfind("Connection closed");
    size_t ha_required = logs.rfind("auth_required");
    bool ha_connected = ha_ok != std::string::npos &&
                        (ha_closed == std::string::npos || ha_ok > ha_closed) &&
                        (ha_required == std::string::npos || ha_ok > ha_required);

    size_t mqtt_ok = logs.rfind("Connected successfully to broker");
    size_t mqtt_failed = logs.rfind("Socket connection failed");
    bool mqtt_connected = mqtt_ok != std::string::npos &&
                          (mqtt_failed == std::string::npos || mqtt_ok > mqtt_failed);

    long uptime_seconds = 0;
    std::ifstream uptime_file("/proc/uptime");
    if (uptime_file.is_open()) uptime_file >> uptime_seconds;

    int brightness = 0;
    int max_brightness = 255;
    std::ifstream brightness_file("/sys/class/backlight/backlight/brightness");
    std::ifstream max_brightness_file("/sys/class/backlight/backlight/max_brightness");
    if (brightness_file.is_open()) brightness_file >> brightness;
    if (max_brightness_file.is_open()) max_brightness_file >> max_brightness;
    int brightness_percent = max_brightness > 0 ? (brightness * 100 + max_brightness / 2) / max_brightness : 0;

    bool app_running = false;
    FILE * pid_pipe = popen("pidof ha_panel", "r");
    if (pid_pipe) {
        int pid = 0;
        while (fscanf(pid_pipe, "%d", &pid) == 1) {
            if (pid != (int)getpid()) app_running = true;
        }
        pclose(pid_pipe);
    }

    std::string active_app = "none";
    if (app_running) {
        active_app = "ha_panel";
    } else {
        FILE * ps_pipe = popen("ps w | grep -v grep | grep -E 'voice_control'", "r");
        if (ps_pipe) {
            char buf[256];
            while (fgets(buf, sizeof(buf), ps_pipe)) {
                std::string line = buf;
                if (line.find(" Z ") != std::string::npos) continue;
                if (line.find("voice_control_factory") != std::string::npos) {
                    active_app = "tuya_factory";
                    break;
                } else if (line.find("voice_control") != std::string::npos) {
                    active_app = "tuya_gui";
                    break;
                }
            }
            pclose(ps_pipe);
        }
    }

    std::stringstream json;
    json << "{"
         << "\"ip\":\"" << json_escape(get_wlan0_ip()) << "\","
         << "\"version\":\"" << json_escape(CURRENT_VERSION) << "\","
         << "\"uptime_seconds\":" << uptime_seconds << ","
         << "\"brightness\":" << brightness_percent << ","
         << "\"app_running\":" << (app_running ? "true" : "false") << ","
         << "\"ha_configured\":" << (!ha_url.empty() && !ha_token.empty() ? "true" : "false") << ","
         << "\"ha_connected\":" << (ha_connected ? "true" : "false") << ","
         << "\"ha_token_set\":" << (!ha_token.empty() ? "true" : "false") << ","
         << "\"ha_url\":\"" << json_escape(ha_url) << "\","
         << "\"entity_1\":\"" << json_escape(ha_entity_1) << "\","
         << "\"entity_1_name\":\"" << json_escape(ha_entity_1_name) << "\","
         << "\"entity_2\":\"" << json_escape(ha_entity_2) << "\","
         << "\"entity_2_name\":\"" << json_escape(ha_entity_2_name) << "\","
         << "\"mqtt_enabled\":" << (g_mqtt_config.enabled ? "true" : "false") << ","
         << "\"mqtt_connected\":" << (mqtt_connected ? "true" : "false") << ","
         << "\"mqtt_host\":\"" << json_escape(g_mqtt_config.host) << "\","
         << "\"mqtt_port\":" << g_mqtt_config.port << ","
         << "\"mqtt_user\":\"" << json_escape(g_mqtt_config.username) << "\","
         << "\"mqtt_pass_set\":" << (!g_mqtt_config.password.empty() ? "true" : "false") << ","
         << "\"mqtt_topic\":\"" << json_escape(g_mqtt_config.base_topic) << "\","
         << "\"mqtt_discovery\":" << (g_mqtt_config.discovery ? "true" : "false") << ","
         << "\"web_autostart\":" << (g_web_autostart ? "true" : "false") << ","
         << "\"ha_autostart\":" << (g_ha_autostart ? "true" : "false") << ","
         << "\"active_app\":\"" << json_escape(active_app) << "\""
         << "}";
    cgi_json_response(200, json.str());
    return 0;
}

struct HaControl {
    std::string entity_id;
    std::string name;
    lv_obj_t *button;
    lv_obj_t *label;
};

static HaControl ha_controls[2];

static bool ha_request(const std::string &method, const std::string &endpoint,
                       const std::string &body, std::string *response_body) {
    std::string url = ha_url;
    while (!url.empty() && (url.back() == '/' || url.back() == ' ' || url.back() == '\r' || url.back() == '\n')) url.pop_back();

    std::string full_url = url + endpoint;
    std::string curl_bin = (access("/tuya/data/curl", X_OK) == 0) ? "/tuya/data/curl" : "curl";

    std::stringstream cmd;
    cmd << curl_bin << " -s -k -m 5 -i -X " << method;
    cmd << " -H \"Authorization: Bearer " << ha_token << "\"";
    cmd << " -H \"Accept: application/json\"";
    if (!body.empty()) {
        cmd << " -H \"Content-Type: application/json\"";
        cmd << " -d '" << body << "'";
    }
    cmd << " \"" << full_url << "\" 2>/dev/null";

    FILE *pipe = popen(cmd.str().c_str(), "r");
    if (!pipe) return false;

    std::string response;
    char buf[1024];
    while (fgets(buf, sizeof(buf), pipe) != NULL) {
        response.append(buf);
    }
    pclose(pipe);

    if (response.empty()) return false;

    int status = 0;
    sscanf(response.c_str(), "HTTP/%*s %d", &status);
    size_t separator = response.find("\r\n\r\n");
    if (response_body) {
        *response_body = separator == std::string::npos ? "" : response.substr(separator + 4);
    }
    return status >= 200 && status < 300;
}

static void publish_panel_state_to_ha(void) {
    if (ha_url.empty() || ha_token.empty()) return;

    std::string ip = get_wlan0_ip();

    // 1. Publish binary_sensor.moes_panel_screen (on = active, off = blanked)
    std::string screen_state = g_screen_blanked ? "off" : "on";
    std::stringstream screen_body;
    screen_body << "{\"state\":\"" << screen_state << "\",\"attributes\":{"
                << "\"friendly_name\":\"MOES Panel - Ekran\","
                << "\"ip_address\":\"" << ip << "\","
                << "\"version\":\"" << CURRENT_VERSION << "\"}}";
    
    ha_request("POST", "/api/states/binary_sensor.moes_panel_screen", screen_body.str(), NULL);

    // 2. Publish sensor.moes_panel_status (online)
    std::stringstream status_body;
    status_body << "{\"state\":\"online\",\"attributes\":{"
                << "\"friendly_name\":\"MOES Panel - Status\","
                << "\"ip_address\":\"" << ip << "\","
                << "\"version\":\"" << CURRENT_VERSION << "\","
                << "\"screen_timeout_s\":" << (screen_timeout_ms / 1000) << "}}";

    ha_request("POST", "/api/states/sensor.moes_panel_status", status_body.str(), NULL);
}

struct ControlStateAsyncData {
    HaControl *control;
    bool is_on;
};

static void update_control_state_async(void * user_data) {
    ControlStateAsyncData * data = (ControlStateAsyncData *)user_data;
    if (data && data->control && data->control->button) {
        if (data->is_on) {
            lv_obj_add_state(data->control->button, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(data->control->button, LV_STATE_CHECKED);
        }
    }
    delete data;
}

static std::atomic<bool> ws_connected{false};
static std::thread * ws_thread_ptr = NULL;
static std::atomic<bool> ws_thread_running{false};

static std::string ws_encode_frame(const std::string &message) {
    std::string frame;
    frame.push_back((char)0x81); // FIN = 1, Opcode = 1 (text)
    size_t len = message.size();
    if (len <= 125) {
        frame.push_back((char)(0x80 | len));
    } else if (len <= 65535) {
        frame.push_back((char)(0x80 | 126));
        frame.push_back((char)((len >> 8) & 0xFF));
        frame.push_back((char)(len & 0xFF));
    } else {
        frame.push_back((char)(0x80 | 127));
        for (int i = 7; i >= 0; i--) {
            frame.push_back((char)((len >> (i * 8)) & 0xFF));
        }
    }
    char mask[4] = {0x12, 0x34, 0x56, 0x78};
    frame.append(mask, 4);
    for (size_t i = 0; i < len; i++) {
        frame.push_back(message[i] ^ mask[i % 4]);
    }
    return frame;
}

static bool parse_ha_url_components(std::string *host, int *port, std::string *base_path) {
    std::string url = ha_url;
    while (!url.empty() && ((uint8_t)url.back() <= 32)) url.pop_back();
    while (!url.empty() && ((uint8_t)url.front() <= 32)) url.erase(0, 1);

    if (url.empty()) return false;

    if (url.compare(0, 8, "https://") == 0) {
        url = url.substr(8);
    } else if (url.compare(0, 7, "http://") == 0) {
        url = url.substr(7);
    }

    size_t slash = url.find('/');
    std::string authority = slash == std::string::npos ? url : url.substr(0, slash);
    *base_path = slash == std::string::npos ? "" : url.substr(slash);
    while (!base_path->empty() && base_path->back() == '/') base_path->pop_back();

    size_t colon = authority.rfind(':');
    *host = colon == std::string::npos ? authority : authority.substr(0, colon);
    *port = colon == std::string::npos ? 8123 : atoi(authority.substr(colon + 1).c_str());

    while (!host->empty() && ((uint8_t)host->back() <= 32)) host->pop_back();
    while (!host->empty() && ((uint8_t)host->front() <= 32)) host->erase(0, 1);

    return !host->empty() && *port > 0 && *port <= 65535;
}

static bool ws_read_bytes_mbed(mbedtls_ssl_context *ssl, char *dest, size_t len) {
    size_t read_bytes = 0;
    while (read_bytes < len) {
        int r = mbedtls_ssl_read(ssl, (unsigned char*)dest + read_bytes, len - read_bytes);
        if (r <= 0) return false;
        read_bytes += r;
    }
    return true;
}

static bool ws_read_frame_mbed(mbedtls_ssl_context *ssl, std::string &out_payload) {
    char hdr[2];
    if (!ws_read_bytes_mbed(ssl, hdr, 2)) return false;

    uint8_t mask_len = (uint8_t)hdr[1];
    bool has_mask = (mask_len & 0x80) != 0;
    size_t len = mask_len & 0x7F;

    if (len == 126) {
        char len_bytes[2];
        if (!ws_read_bytes_mbed(ssl, len_bytes, 2)) return false;
        len = ((uint8_t)len_bytes[0] << 8) | (uint8_t)len_bytes[1];
    } else if (len == 127) {
        char len_bytes[8];
        if (!ws_read_bytes_mbed(ssl, len_bytes, 8)) return false;
        len = 0;
        for (int i = 0; i < 8; i++) {
            len = (len << 8) | (uint8_t)len_bytes[i];
        }
    }

    char mask_key[4] = {0};
    if (has_mask) {
        if (!ws_read_bytes_mbed(ssl, mask_key, 4)) return false;
    }

    out_payload.resize(len);
    if (len > 0) {
        if (!ws_read_bytes_mbed(ssl, &out_payload[0], len)) return false;
    }

    if (has_mask) {
        for (size_t i = 0; i < len; i++) {
            out_payload[i] ^= mask_key[i % 4];
        }
    }
    return true;
}

static void ws_send_frame_mbed(mbedtls_ssl_context *ssl, const std::string &message) {
    std::string frame = ws_encode_frame(message);
    mbedtls_ssl_write(ssl, (const unsigned char *)frame.data(), frame.size());
}

static int connect_socket_mbed(const std::string &host, int port, mbedtls_net_context *net_ctx) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in sa = {};
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);

    if (inet_pton(AF_INET, host.c_str(), &sa.sin_addr) <= 0) {
        struct addrinfo hints = {}, *res = NULL;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        char port_text[16];
        snprintf(port_text, sizeof(port_text), "%d", port);
        if (getaddrinfo(host.c_str(), port_text, &hints, &res) != 0 || !res) {
            close(fd);
            return -1;
        }
        memcpy(&sa, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);
    }

    struct timeval tv = {3, 0};
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    if (connect(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        printf("[WebSocket Connect Error] connect to %s:%d failed: errno=%d %s\n", host.c_str(), port, errno, strerror(errno));
        close(fd);
        return -1;
    }

    net_ctx->fd = fd;
    return 0;
}

static void ws_thread_loop() {
    printf("[WebSocket ⚡] Native MbedTLS thread starting for URL: %s...\n", ha_url.c_str());
    while (ws_thread_running) {
        if (ha_url.empty() || ha_token.empty()) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }

        std::string host;
        int port = 8123;
        std::string base_path;
        if (!parse_ha_url_components(&host, &port, &base_path)) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        mbedtls_net_context server_fd;
        mbedtls_entropy_context entropy;
        mbedtls_ctr_drbg_context ctr_drbg;
        mbedtls_ssl_context ssl;
        mbedtls_ssl_config conf;

        mbedtls_net_init(&server_fd);
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_ctr_drbg_init(&ctr_drbg);
        mbedtls_entropy_init(&entropy);

        mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)"ha_panel", 8);

        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        if (connect_socket_mbed(host, port, &server_fd) != 0) {
            printf("[WebSocket ❌] TCP Connection to %s:%d failed. Retrying in 3s...\n", host.c_str(), port);
            mbedtls_net_free(&server_fd);
            mbedtls_ssl_free(&ssl);
            mbedtls_ssl_config_free(&conf);
            mbedtls_ctr_drbg_free(&ctr_drbg);
            mbedtls_entropy_free(&entropy);
            std::this_thread::sleep_for(std::chrono::seconds(3));
            continue;
        }

        mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

        mbedtls_ssl_setup(&ssl, &conf);
        mbedtls_ssl_set_hostname(&ssl, host.c_str());
        mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send, mbedtls_net_recv, NULL);

        int ret = 0;
        while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
            if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
                printf("[WebSocket ❌] TLS Handshake failed: -0x%x\n", -ret);
                break;
            }
        }

        if (ret == 0) {
            printf("[WebSocket 🟢] MbedTLS Handshake OK! Sending HTTP WebSocket Upgrade...\n");
            std::string req = 
                "GET " + base_path + "/api/websocket HTTP/1.1\r\n"
                "Host: " + host + ":" + port_str + "\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n";

            mbedtls_ssl_write(&ssl, (const unsigned char *)req.data(), req.size());

            std::string header_buf;
            char c;
            bool header_ok = false;
            while (ws_read_bytes_mbed(&ssl, &c, 1)) {
                header_buf.push_back(c);
                if (header_buf.size() >= 4 && header_buf.substr(header_buf.size() - 4) == "\r\n\r\n") {
                    header_ok = true;
                    break;
                }
                if (header_buf.size() > 4096) break;
            }

            if (header_ok && header_buf.find("101") != std::string::npos) {
                printf("[WebSocket 🟢] HTTP 101 Upgrade Successful!\n");
                std::string msg;
                while (ws_thread_running && ws_read_frame_mbed(&ssl, msg)) {
                    if (msg.find("auth_required") != std::string::npos) {
                        printf("[WebSocket 🔑] Received auth_required. Authenticating...\n");
                        std::string auth = "{\"type\":\"auth\",\"access_token\":\"" + ha_token + "\"}";
                        ws_send_frame_mbed(&ssl, auth);
                    } else if (msg.find("auth_ok") != std::string::npos) {
                        printf("[WebSocket 🟢🟢] AUTH OK! Subscribing to real-time events...\n");
                        ws_connected = true;

                        std::string sub = "{\"id\":1,\"type\":\"subscribe_events\",\"event_type\":\"state_changed\"}";
                        ws_send_frame_mbed(&ssl, sub);

                        std::string get_st = "{\"id\":2,\"type\":\"get_states\"}";
                        ws_send_frame_mbed(&ssl, get_st);
            } else if (msg.find("state_changed") != std::string::npos || msg.find("entity_id") != std::string::npos) {
                size_t new_st_pos = msg.find("\"new_state\"");
                std::string new_st_json = (new_st_pos != std::string::npos) ? msg.substr(new_st_pos) : msg;

                for (int i = 0; i < 2; i++) {
                    if (ha_controls[i].entity_id.empty()) continue;
                    size_t pos = new_st_json.find(ha_controls[i].entity_id);
                    if (pos != std::string::npos) {
                        size_t st_pos = new_st_json.find("\"state\":", pos);
                        if (st_pos != std::string::npos) {
                            size_t q1 = new_st_json.find("\"", st_pos + 8);
                            if (q1 != std::string::npos) {
                                size_t q2 = new_st_json.find("\"", q1 + 1);
                                if (q2 != std::string::npos) {
                                    std::string st = new_st_json.substr(q1 + 1, q2 - q1 - 1);
                                    if (st == "on" || st == "off") {
                                        bool is_on = (st == "on");
                                        printf("[WebSocket Live Event ⚡] %s -> %s\n", ha_controls[i].entity_id.c_str(), st.c_str());
                                        lv_async_call(update_control_state_async, new ControlStateAsyncData{&ha_controls[i], is_on});
                                    }
                                }
                            }
                        }
                    }
                }
            }
                }
            } else {
                printf("[WebSocket ❌] HTTP Upgrade failed or rejected by HA.\n");
            }
        }

        ws_connected = false;
        mbedtls_net_free(&server_fd);
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_ctr_drbg_free(&ctr_drbg);
        mbedtls_entropy_free(&entropy);

        printf("[WebSocket] Connection closed. Reconnecting in 3s...\n");
        std::this_thread::sleep_for(std::chrono::seconds(3));
    }
}

static void start_websocket_thread() {
    if (ws_thread_running) return;
    printf("[WebSocket] Spawning background thread...\n");
    ws_thread_running = true;
    ws_thread_ptr = new std::thread(ws_thread_loop);
}

static void update_control_state(HaControl *control) {
    if (!control || !control->button || control->entity_id.empty()) return;
    std::string response;
    if (!ha_request("GET", "/api/states/" + control->entity_id, "", &response)) {
        lv_obj_set_style_bg_color(control->button, lv_color_make(0x66, 0x55, 0x00), LV_PART_MAIN);
        return;
    }
    std::string state = parse_json_value(response, "state");
    if (state == "on") {
        lv_obj_add_state(control->button, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(control->button, LV_STATE_CHECKED);
    }
}

static void state_poll_timer_cb(lv_timer_t * timer) {
    (void)timer;
    update_control_state(&ha_controls[0]);
    update_control_state(&ha_controls[1]);
    publish_panel_state_to_ha();
}

struct OtaModalData {
    lv_obj_t * mbox;
    lv_obj_t * bar;
};

// Perform OTA self-replacement update directly from GitHub Releases API
bool perform_github_ota(lv_obj_t * mbox, lv_obj_t * bar) {
    printf("[OTA] Starting OTA process...\n");
    
    // Ensure default route is set for internet connectivity
    system("ip route add default via 192.168.1.1 dev wlan0 2>/dev/null");

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
    size_t expected_size = parse_json_int_value(json, "size");
    
    if (tag_name.empty() || download_url.empty()) {
        printf("[OTA] Error: Failed to parse tag_name or download URL. Releases might be empty.\n");
        return false;
    }

    if (expected_size == 0) expected_size = 2300000; // Fallback default ~2.3MB
    
    printf("[OTA] Latest version: %s (Current: %s), Size: %zu bytes\n", tag_name.c_str(), CURRENT_VERSION, expected_size);
    if (tag_name == CURRENT_VERSION) {
        printf("[OTA] Panel is already up to date!\n");
        return false;
    }
    
    // 2. Download the binary asset from public URL
    unlink("/tuya/data/ha_panel.tmp");
    printf("[OTA] Downloading new binary asset from %s...\n", download_url.c_str());
    std::string cmd_download = "wget -q --header=\"User-Agent: Mozilla/5.0\" "
                               "-O /tuya/data/ha_panel.tmp " + download_url + " &";
                               
    system(cmd_download.c_str());

    // Monitor download progress
    int last_pct = -1;
    uint32_t start_ticks = custom_tick_get();
    while (custom_tick_get() - start_ticks < 60000) { // 60s timeout max
        struct stat st;
        if (stat("/tuya/data/ha_panel.tmp", &st) == 0) {
            int pct = (int)((st.st_size * 100) / expected_size);
            if (pct > 100) pct = 100;

            if (pct != last_pct) {
                last_pct = pct;
                if (bar) lv_bar_set_value(bar, pct, LV_ANIM_OFF);
                if (mbox) {
                    char status_txt[128];
                    snprintf(status_txt, sizeof(status_txt), "Pobieranie: %d%% (%ld / %ld KB)", pct, (long)(st.st_size / 1024), (long)(expected_size / 1024));
                    lv_label_set_text(lv_msgbox_get_text(mbox), status_txt);
                }
            }

            if (st.st_size >= (off_t)expected_size && system("pidof wget >/dev/null") != 0) {
                if (bar) lv_bar_set_value(bar, 100, LV_ANIM_OFF);
                if (mbox) lv_label_set_text(lv_msgbox_get_text(mbox), "Instalowanie i restart...");
                lv_timer_handler();
                usleep(300000);
                break;
            }
        }
        lv_timer_handler();
        usleep(100000); // 100ms
    }

    if (access("/tuya/data/ha_panel.tmp", F_OK) != 0) {
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
    if (code != LV_EVENT_CLICKED) return;

    HaControl *control = (HaControl *)lv_event_get_user_data(e);
    if (!control || control->entity_id.empty()) return;
    size_t dot = control->entity_id.find('.');
    if (dot == std::string::npos) return;
    std::string domain = control->entity_id.substr(0, dot);
    if (domain != "light" && domain != "fan" && domain != "switch" && domain != "input_boolean") return;

    // LVGL automatically toggles LV_STATE_CHECKED on click for CHECKABLE buttons.
    // Dispatch service call in background thread (non-blocking)
    std::string entity_id = control->entity_id;
    std::thread([domain, entity_id]() {
        std::string body = "{\"entity_id\":\"" + entity_id + "\"}";
        if (!ha_request("POST", "/api/services/" + domain + "/toggle", body, NULL)) {
            printf("[HA] Async toggle failed for %s.\n", entity_id.c_str());
        }
    }).detach();
}

static void msgbox_close_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        lv_msgbox_close(lv_event_get_current_target(e));
    }
}

static void ota_update_timer_cb(lv_timer_t * timer) {
    OtaModalData * data = (OtaModalData *)timer->user_data;
    lv_timer_del(timer);

    if (!perform_github_ota(data->mbox, data->bar)) {
        lv_msgbox_close(data->mbox);
        delete data;
        static const char * err_btns[] = {"ZAMKNIJ", ""};
        lv_obj_t * err_mbox = lv_msgbox_create(lv_layer_top(), "BŁĄD", "Aktualizacja nie powiodła się!\nSprawdź połączenie lub wersję.", err_btns, false);
        lv_obj_add_event_cb(err_mbox, msgbox_close_cb, LV_EVENT_VALUE_CHANGED, NULL);
        lv_obj_align(err_mbox, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_width(err_mbox, 360);
        lv_obj_set_style_bg_color(err_mbox, lv_color_make(0x2D, 0x2D, 0x2D), LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_msgbox_get_title(err_mbox), lv_color_make(0xF4, 0x43, 0x36), LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_msgbox_get_text(err_mbox), lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_text_color(lv_msgbox_get_btns(err_mbox), lv_color_make(0x20, 0x20, 0x20), LV_PART_ITEMS);
    }
}

enum settings_action_t {
    SETTINGS_ACTION_NONE = 0,
    SETTINGS_ACTION_CONTROLS,
    SETTINGS_ACTION_DISPLAY,
    SETTINGS_ACTION_WIFI,
    SETTINGS_ACTION_DIAGNOSTICS,
    SETTINGS_ACTION_INFO,
    SETTINGS_ACTION_UPDATES,
    SETTINGS_ACTION_MQTT,
    SETTINGS_ACTION_WEB_PORTAL
};

static void create_updates_screen(void);
static void create_diagnostics_screen(void);
static void create_info_screen(void);
static void create_mqtt_screen(void);
static void create_display_screen(void);
static void create_wifi_screen(void);
static void close_wifi_screen(void);
static void create_web_portal_screen(void);
static void close_web_portal_screen(void);
static int read_int_file(const char * path, int fallback);
static void set_percent_label(lv_obj_t * label, int value);
static void set_auto_brightness_enabled(bool enabled);

static lv_obj_t * mqtt_screen = NULL;
static lv_obj_t * mqtt_kb = NULL;
static lv_obj_t * mqtt_sw_en = NULL;
static lv_obj_t * mqtt_ta_host = NULL;
static lv_obj_t * mqtt_ta_port = NULL;
static lv_obj_t * mqtt_ta_user = NULL;
static lv_obj_t * mqtt_ta_pass = NULL;
static lv_obj_t * mqtt_ta_topic = NULL;
static lv_obj_t * mqtt_sw_disc = NULL;

static void close_mqtt_screen(void) {
    if (mqtt_kb) {
        lv_obj_del(mqtt_kb);
        mqtt_kb = NULL;
    }
    if (mqtt_screen) {
        lv_obj_del_async(mqtt_screen);
        mqtt_screen = NULL;
    }
}

static void mqtt_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_mqtt_screen();
}

static void mqtt_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);

    if (code == LV_EVENT_FOCUSED) {
        if (!mqtt_kb) {
            mqtt_kb = lv_keyboard_create(lv_layer_top());
            lv_obj_set_size(mqtt_kb, 480, 220);
            lv_obj_align(mqtt_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_text_font(mqtt_kb, &lv_font_montserrat_16_pl, LV_PART_ITEMS);
        }
        lv_keyboard_set_textarea(mqtt_kb, ta);
        lv_obj_clear_flag(mqtt_kb, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY) {
        if (mqtt_kb) {
            lv_obj_add_flag(mqtt_kb, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void save_mqtt_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    g_mqtt_config.enabled = lv_obj_has_state(mqtt_sw_en, LV_STATE_CHECKED);
    g_mqtt_config.host = lv_textarea_get_text(mqtt_ta_host);
    
    int port_val = atoi(lv_textarea_get_text(mqtt_ta_port));
    g_mqtt_config.port = port_val > 0 ? port_val : 1883;

    g_mqtt_config.username = lv_textarea_get_text(mqtt_ta_user);
    g_mqtt_config.password = lv_textarea_get_text(mqtt_ta_pass);
    g_mqtt_config.base_topic = lv_textarea_get_text(mqtt_ta_topic);
    g_mqtt_config.discovery = lv_obj_has_state(mqtt_sw_disc, LV_STATE_CHECKED);

    save_configuration();

    g_mqtt_client.stop();
    g_mqtt_client.configure(g_mqtt_config);
    if (g_mqtt_config.enabled) {
        g_mqtt_client.start();
    }

    close_mqtt_screen();
}

static void close_updates_screen(void) {
    if (updates_screen) {
        lv_obj_del_async(updates_screen);
        updates_screen = NULL;
    }
}

static void close_diagnostics_screen(void) {
    if (diagnostics_screen) {
        lv_obj_del_async(diagnostics_screen);
        diagnostics_screen = NULL;
    }
}

static void close_info_screen(void) {
    if (info_screen) {
        lv_obj_del_async(info_screen);
        info_screen = NULL;
    }
}

static void close_display_screen(void) {
    if (display_screen) {
        lv_obj_del_async(display_screen);
        display_screen = NULL;
        display_auto_switch = NULL;
        display_auto_status_label = NULL;
        display_brightness_slider = NULL;
        display_brightness_label = NULL;
        for (int i = 0; i < 4; ++i) display_timeout_buttons[i] = NULL;
    }
}

static void close_wifi_screen(void) {
    if (wifi_screen) {
        lv_obj_del_async(wifi_screen);
        wifi_screen = NULL;
        wifi_status_dot = NULL;
        wifi_status_label = NULL;
        wifi_switch = NULL;
        wifi_avail_container = NULL;
        wifi_scan_btn_label = NULL;
    }
}

static void close_settings(void) {
    close_updates_screen();
    close_diagnostics_screen();
    close_info_screen();
    close_display_screen();
    close_wifi_screen();
    close_mqtt_screen();
    close_web_portal_screen();
    if (!settings_screen) return;
    lv_obj_del_async(settings_screen);
    settings_screen = NULL;
}

static void settings_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_settings();
}

// Event callback for the Info button "?"
static void info_btn_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) create_info_screen();
}

static void settings_card_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_action_t action = (settings_action_t)(long)lv_event_get_user_data(e);

    if (action == SETTINGS_ACTION_CONTROLS) {
        close_settings();
        lv_obj_set_y(control_center, 0);
    } else if (action == SETTINGS_ACTION_DISPLAY) {
        create_display_screen();
    } else if (action == SETTINGS_ACTION_WIFI) {
        create_wifi_screen();
    } else if (action == SETTINGS_ACTION_DIAGNOSTICS) {
        create_diagnostics_screen();
    } else if (action == SETTINGS_ACTION_INFO) {
        create_info_screen();
    } else if (action == SETTINGS_ACTION_UPDATES) {
        create_updates_screen();
    } else if (action == SETTINGS_ACTION_MQTT) {
        create_mqtt_screen();
    } else if (action == SETTINGS_ACTION_WEB_PORTAL) {
        create_web_portal_screen();
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

static lv_obj_t * create_subscreen_base(const char * title, lv_event_cb_t back_cb, lv_obj_t ** list_out) {
    lv_obj_t * scr = lv_obj_create(lv_layer_top());
    lv_obj_set_size(scr, 480, 480);
    lv_obj_set_pos(scr, 0, 0);
    lv_obj_set_style_bg_color(scr, lv_color_make(0x11, 0x13, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(scr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(scr, 0, LV_PART_MAIN);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * list = lv_obj_create(scr);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * header_bar = lv_obj_create(scr);
    lv_obj_set_size(header_bar, 480, 70);
    lv_obj_set_pos(header_bar, 0, 0);
    lv_obj_set_style_bg_color(header_bar, lv_color_make(0x11, 0x13, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * back = lv_btn_create(header_bar);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    lv_obj_set_style_bg_color(back, lv_color_make(0x2A, 0x2D, 0x35), LV_PART_MAIN);
    lv_obj_set_style_radius(back, 24, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(back, 0, LV_PART_MAIN);
    if (back_cb) lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * back_icon = lv_label_create(back);
    lv_label_set_text(back_icon, ICON_BACK);
    lv_obj_set_style_text_font(back_icon, &lv_font_control_icons_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(back_icon, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(back_icon, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * heading = lv_label_create(header_bar);
    lv_label_set_text(heading, title);
    lv_obj_set_pos(heading, 76, 22);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_make(0xE4, 0xE2, 0xE9), LV_PART_MAIN);

    if (list_out) *list_out = list;
    return scr;
}

static const lv_color_t M3_SURFACE_CONTAINER = lv_color_hex(0x1D2024);
static const lv_color_t M3_SURFACE_HIGH = lv_color_hex(0x282A2F);
static const lv_color_t M3_OUTLINE = lv_color_hex(0x8D9199);
static const lv_color_t M3_OUTLINE_VARIANT = lv_color_hex(0x43474E);
static const lv_color_t M3_ON_SURFACE = lv_color_hex(0xE2E2E8);
static const lv_color_t M3_ON_SURFACE_VARIANT = lv_color_hex(0xC3C6CF);
static const lv_color_t M3_PRIMARY = lv_color_hex(0x4FD8E6);
static const lv_color_t M3_ON_PRIMARY = lv_color_hex(0x00363D);
static const lv_color_t M3_PRIMARY_CONTAINER = lv_color_hex(0x004F58);
static const lv_color_t M3_ON_PRIMARY_CONTAINER = lv_color_hex(0x9CF0FA);
static const lv_color_t M3_SUCCESS = lv_color_hex(0x91D18B);

static void settings_m3_surface(lv_obj_t * obj, lv_color_t color, int radius) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t * settings_m3_label(lv_obj_t * parent, const char * text, int x, int y,
                                    lv_color_t color, const lv_font_t * font) {
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    const lv_font_t * target_font = font;
    if (font == &lv_font_montserrat_12) target_font = &lv_font_montserrat_12_pl;
    else if (font == &lv_font_montserrat_14) target_font = &lv_font_montserrat_14_pl;
    else if (font == &lv_font_montserrat_16) target_font = &lv_font_montserrat_16_pl;
    else if (font == &lv_font_montserrat_20) target_font = &lv_font_montserrat_20_pl;
    else if (font == &lv_font_montserrat_24) target_font = &lv_font_montserrat_24_pl;
    lv_obj_set_style_text_font(label, target_font, LV_PART_MAIN);
    return label;
}

static lv_obj_t * icon_badge(lv_obj_t * parent, const char * symbol, int x, int y,
                            lv_color_t background, lv_color_t foreground) {
    lv_obj_t * badge = lv_obj_create(parent);
    lv_obj_set_size(badge, 44, 44);
    lv_obj_set_pos(badge, x, y);
    settings_m3_surface(badge, background, 22);
    lv_obj_t * glyph = settings_m3_label(badge, symbol, 0, 0, foreground, &lv_font_control_icons_24);
    lv_obj_center(glyph);
    return badge;
}

static lv_obj_t * settings_m3_card(lv_obj_t * parent, int x, int y, int width, int height) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_pos(card, x, y);
    settings_m3_surface(card, M3_SURFACE_CONTAINER, 24);
    return card;
}

static void settings_m3_style_switch(lv_obj_t * sw) {
    lv_obj_set_size(sw, 48, 28);
    lv_obj_set_style_bg_color(sw, M3_SURFACE_HIGH, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, M3_OUTLINE, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, M3_OUTLINE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, M3_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, M3_ON_PRIMARY, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN | LV_STATE_CHECKED);
}

static void settings_m3_hero(lv_obj_t * parent, const char * title, const char * subtitle,
                             const char * icon, bool active) {
    lv_obj_t * hero = settings_m3_card(parent, 20, 8, 440, 76);
    lv_obj_set_style_bg_color(hero, M3_PRIMARY_CONTAINER, LV_PART_MAIN);

    lv_obj_t * dot = lv_obj_create(hero);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_pos(dot, 16, 22);
    settings_m3_surface(dot, active ? M3_SUCCESS : M3_OUTLINE, 4);
    settings_m3_label(hero, title, 34, 10, M3_ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    settings_m3_label(hero, subtitle, 16, 44, lv_color_hex(0x8BD7DF), &lv_font_montserrat_14);

    lv_obj_t * badge = lv_obj_create(hero);
    lv_obj_set_size(badge, 44, 44);
    lv_obj_set_pos(badge, 380, 16);
    settings_m3_surface(badge, lv_color_hex(0x116872), 22);
    lv_obj_t * glyph = settings_m3_label(badge, icon, 0, 0, M3_ON_PRIMARY_CONTAINER,
                                         &lv_font_control_icons_24);
    lv_obj_center(glyph);
}

static lv_obj_t * settings_m3_button(lv_obj_t * parent, const char * text, int x, int y,
                                     int width, lv_event_cb_t callback) {
    lv_obj_t * button = lv_btn_create(parent);
    lv_obj_set_size(button, width, 48);
    lv_obj_set_pos(button, x, y);
    settings_m3_surface(button, M3_PRIMARY, 24);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x3DC3D1), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, NULL);
    lv_obj_t * button_label = settings_m3_label(button, text, 0, 0, M3_ON_PRIMARY,
                                                &lv_font_montserrat_16_pl);
    lv_obj_center(button_label);
    return button;
}

static lv_obj_t * settings_m3_text_field(lv_obj_t * parent, const char * caption,
                                         const char * value, const char * placeholder,
                                         int x, int y, int width, bool password) {
    settings_m3_label(parent, caption, x + 4, y, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t * textarea = lv_textarea_create(parent);
    lv_obj_set_size(textarea, width, 48);
    lv_obj_set_pos(textarea, x, y + 22);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_placeholder_text(textarea, placeholder);
    lv_textarea_set_text(textarea, value);
    lv_textarea_set_password_mode(textarea, password);
    lv_obj_set_style_text_font(textarea, &lv_font_montserrat_16_pl, LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, M3_ON_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(textarea, M3_SURFACE_HIGH, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, M3_OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, M3_PRIMARY, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(textarea, 14, LV_PART_MAIN);
    lv_obj_add_event_cb(textarea, mqtt_ta_event_cb, LV_EVENT_ALL, NULL);
    return textarea;
}

static void updates_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_updates_screen();
}

static void start_ota_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t * mbox = lv_msgbox_create(lv_layer_top(), "AKTUALIZACJA", "Łączenie z GitHubem...\n0%", NULL, false);
    lv_obj_align(mbox, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_width(mbox, 360);
    lv_obj_set_style_bg_color(mbox, lv_color_make(0x2D, 0x2D, 0x2D), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_title(mbox), lv_color_make(0x03, 0xA9, 0xF4), LV_PART_MAIN);
    lv_obj_set_style_text_color(lv_msgbox_get_text(mbox), lv_color_white(), LV_PART_MAIN);

    lv_obj_t * bar = lv_bar_create(mbox);
    lv_obj_set_size(bar, 300, 16);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x4A, 0x4E, 0x59), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_make(0x03, 0xA9, 0xF4), LV_PART_INDICATOR);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, -10);

    OtaModalData * data = new OtaModalData{mbox, bar};
    lv_timer_create(ota_update_timer_cb, 150, data);
}

static void create_updates_screen(void) {
    if (updates_screen) return;

    lv_obj_t * list = NULL;
    updates_screen = create_subscreen_base("Aktualizacje", updates_back_event_cb, &list);
    std::string ip = get_wlan0_ip();
    bool online = ip != "127.0.0.1";
    std::string hero_title = std::string("Wersja ") + CURRENT_VERSION;
    settings_m3_hero(list, hero_title.c_str(), "Aktualizacja z repozytorium GitHub",
                     ICON_DOWNLOAD, online);

    lv_obj_t * release = settings_m3_card(list, 20, 96, 440, 116);
    settings_m3_label(release, "BIEŻĄCE WYDANIE", 16, 14, M3_ON_SURFACE_VARIANT,
                      &lv_font_montserrat_14);
    settings_m3_label(release, "HA Panel", 16, 42, M3_ON_SURFACE, &lv_font_montserrat_20_pl);
    settings_m3_label(release, "Kanał stabilny  •  GitHub", 16, 76, M3_ON_SURFACE_VARIANT,
                      &lv_font_montserrat_14);
    lv_obj_t * version_badge = lv_obj_create(release);
    lv_obj_set_size(version_badge, 84, 32);
    lv_obj_set_pos(version_badge, 340, 42);
    settings_m3_surface(version_badge, lv_color_hex(0x204E35), 16);
    lv_obj_t * version_label = settings_m3_label(version_badge, CURRENT_VERSION, 0, 0,
                                                 M3_SUCCESS, &lv_font_montserrat_14);
    lv_obj_center(version_label);

    lv_obj_t * network = settings_m3_card(list, 20, 224, 440, 72);
    settings_m3_label(network, "Połączenie z serwerem wydań", 16, 10, M3_ON_SURFACE,
                      &lv_font_montserrat_16_pl);
    settings_m3_label(network, online ? "Gotowe do sprawdzenia" : "Brak połączenia z siecią",
                      16, 40, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t * dot = lv_obj_create(network);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_pos(dot, 406, 31);
    settings_m3_surface(dot, online ? M3_SUCCESS : M3_OUTLINE, 5);

    settings_m3_button(list, "Sprawdź i aktualizuj", 20, 316, 440, start_ota_btn_cb);
}

static void diagnostics_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_diagnostics_screen();
}

static void refresh_diagnostics_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    close_diagnostics_screen();
    create_diagnostics_screen();
}

static void create_diagnostics_screen(void) {
    if (diagnostics_screen) return;

    lv_obj_t * list = NULL;
    diagnostics_screen = create_subscreen_base("Diagnostyka", diagnostics_back_event_cb, &list);

    int y = 8;
    std::string ip = get_wlan0_ip();
    std::string net_status = (ip == "127.0.0.1" ? "Brak połączenia" : "Połączono (" + ip + ")");
    std::string ha_status = config_exists() ? ("Skonfigurowano: " + (ha_url.empty() ? "brak URL" : ha_url)) : "Nieskonfigurowany";
    bool httpd_active = (system("pidof httpd >/dev/null") == 0);

    add_settings_card(list, &y, ICON_TOOLS, lv_color_make(0x5C, 0x60, 0x6A),
                      "Aplikacja", "Status: Aktywna (C++ / LVGL)", SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_WIFI, lv_color_make(0x18, 0x65, 0xA8),
                      "Sieć i IP", net_status.c_str(), SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_HOME, lv_color_make(0x03, 0x78, 0xA6),
                      "Home Assistant", ha_status.c_str(), SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_GLOBE, lv_color_make(0x00, 0x68, 0x74),
                      "Portal HTTP WWW", httpd_active ? "Włączony (port 80)" : "Wyłączony", SETTINGS_ACTION_NONE);

    lv_obj_t * btn = lv_btn_create(list);
    lv_obj_set_size(btn, 424, 54);
    lv_obj_set_pos(btn, 28, y + 10);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x2A, 0x2E, 0x38), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn, lv_color_make(0x3B, 0x40, 0x4E), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 20, LV_PART_MAIN);
    lv_obj_set_style_shadow_width(btn, 0, LV_PART_MAIN);
    lv_obj_add_event_cb(btn, refresh_diagnostics_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "ODŚWIEŻ DIAGNOSTYKĘ");
    lv_obj_set_style_text_color(btn_label, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_16_pl, LV_PART_MAIN);
    lv_obj_align(btn_label, LV_ALIGN_CENTER, 0, 0);

    y += 80;
    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, y);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
}

static void info_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_info_screen();
}

static void create_info_screen(void) {
    if (info_screen) return;

    lv_obj_t * list = NULL;
    info_screen = create_subscreen_base("Informacje", info_back_event_cb, &list);
    std::string ip = get_wlan0_ip();
    settings_m3_hero(list, "TPP01-Z", "Panel Home Assistant", ICON_INFO, true);

    const char * captions[] = {"OPROGRAMOWANIE", "SILNIK UI", "SIEĆ", "PLATFORMA"};
    const char * values[] = {CURRENT_VERSION, "LVGL 8.3.11", ip.c_str(), "AArch64 Linux"};
    for (int i = 0; i < 4; ++i) {
        int x = (i % 2 == 0) ? 20 : 248;
        int y = (i < 2) ? 96 : 200;
        lv_obj_t * spec = settings_m3_card(list, x, y, 212, 92);
        settings_m3_label(spec, captions[i], 14, 14, M3_ON_SURFACE_VARIANT,
                          &lv_font_montserrat_14);
        lv_obj_t * value = settings_m3_label(spec, values[i], 14, 43, M3_ON_SURFACE,
                                              &lv_font_montserrat_16_pl);
        lv_obj_set_width(value, 184);
        lv_label_set_long_mode(value, LV_LABEL_LONG_WRAP);
    }

    lv_obj_t * project = settings_m3_card(list, 20, 304, 440, 62);
    settings_m3_label(project, "Projekt open source", 16, 9, M3_ON_SURFACE,
                      &lv_font_montserrat_16_pl);
    settings_m3_label(project, "GwiezdnySzeryf / HA-LVGL", 16, 35, M3_PRIMARY,
                      &lv_font_montserrat_14);
}

static void create_mqtt_screen(void) {
    if (mqtt_screen) return;

    lv_obj_t * list = NULL;
    mqtt_screen = create_subscreen_base("MQTT", mqtt_back_event_cb, &list);

    bool connected = g_mqtt_config.enabled && g_mqtt_client.is_connected();
    const char * status = connected ? "MQTT połączony" :
                          (g_mqtt_config.enabled ? "Łączenie z MQTT" : "MQTT wyłączony");
    std::string endpoint = g_mqtt_config.host.empty() ? "Broker nie jest skonfigurowany" :
                           g_mqtt_config.host + ":" + std::to_string(g_mqtt_config.port);
    settings_m3_hero(list, status, endpoint.c_str(), ICON_PLUG, connected);

    lv_obj_t * client = settings_m3_card(list, 20, 96, 440, 76);
    settings_m3_label(client, "Klient MQTT", 16, 12, M3_ON_SURFACE, &lv_font_montserrat_20_pl);
    settings_m3_label(client, "Publikowanie danych panelu", 16, 44, M3_ON_SURFACE_VARIANT,
                      &lv_font_montserrat_14);
    mqtt_sw_en = lv_switch_create(client);
    settings_m3_style_switch(mqtt_sw_en);
    lv_obj_set_pos(mqtt_sw_en, 374, 24);
    if (g_mqtt_config.enabled) lv_obj_add_state(mqtt_sw_en, LV_STATE_CHECKED);

    mqtt_ta_host = settings_m3_text_field(list, "Broker", g_mqtt_config.host.c_str(),
                                          "192.168.1.73", 20, 188, 292, false);
    std::string port = std::to_string(g_mqtt_config.port);
    mqtt_ta_port = settings_m3_text_field(list, "Port", port.c_str(), "1883",
                                          324, 188, 136, false);
    mqtt_ta_user = settings_m3_text_field(list, "Użytkownik", g_mqtt_config.username.c_str(),
                                          "opcjonalnie", 20, 266, 212, false);
    mqtt_ta_pass = settings_m3_text_field(list, "Hasło", g_mqtt_config.password.c_str(),
                                          "opcjonalnie", 248, 266, 212, true);
    mqtt_ta_topic = settings_m3_text_field(list, "Temat bazowy", g_mqtt_config.base_topic.c_str(),
                                           "panel/tpp01", 20, 344, 440, false);

    lv_obj_t * discovery = settings_m3_card(list, 20, 430, 440, 76);
    settings_m3_label(discovery, "Home Assistant Discovery", 16, 12, M3_ON_SURFACE,
                      &lv_font_montserrat_16_pl);
    settings_m3_label(discovery, "Automatyczne dodawanie encji", 16, 43, M3_ON_SURFACE_VARIANT,
                      &lv_font_montserrat_14);
    mqtt_sw_disc = lv_switch_create(discovery);
    settings_m3_style_switch(mqtt_sw_disc);
    lv_obj_set_pos(mqtt_sw_disc, 374, 24);
    if (g_mqtt_config.discovery) lv_obj_add_state(mqtt_sw_disc, LV_STATE_CHECKED);

    settings_m3_button(list, "Zapisz i połącz", 244, 526, 216, save_mqtt_btn_cb);
    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 24);
    lv_obj_set_pos(spacer, 0, 584);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
}

static bool discover_ambient_light_sensor(void) {
    DIR * directory = opendir("/sys/bus/iio/devices");
    if (!directory) return false;

    struct dirent * entry;
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name, "iio:device", 10) != 0) continue;
        std::string base = std::string("/sys/bus/iio/devices/") + entry->d_name;
        std::ifstream name_file((base + "/name").c_str());
        std::string name;
        if (name_file.is_open()) name_file >> name;
        if (name == "stk3310") {
            ambient_raw_path = base + "/in_illuminance_raw";
            ambient_scale_path = base + "/in_illuminance_scale";
            closedir(directory);
            printf("[AutoBrightness] Found STK3310 at %s.\n", base.c_str());
            return true;
        }
    }

    closedir(directory);
    return false;
}

static float read_ambient_lux(void) {
    if (ambient_raw_path.empty() && !discover_ambient_light_sensor()) return -1.0f;
    std::ifstream raw_file(ambient_raw_path.c_str());
    std::ifstream scale_file(ambient_scale_path.c_str());
    float raw = -1.0f;
    float scale = 1.0f;
    if (raw_file.is_open()) raw_file >> raw;
    if (scale_file.is_open()) scale_file >> scale;
    return raw < 0.0f ? -1.0f : raw * scale;
}

static int ambient_lux_to_brightness(float lux) {
    static const float lux_points[] = {0.0f, 5.0f, 20.0f, 100.0f, 300.0f, 1000.0f, 3000.0f};
    static const int brightness_points[] = {10, 15, 25, 45, 65, 85, 100};
    const int count = sizeof(brightness_points) / sizeof(brightness_points[0]);
    if (lux <= lux_points[0]) return brightness_points[0];
    for (int i = 1; i < count; ++i) {
        if (lux <= lux_points[i]) {
            float ratio = (lux - lux_points[i - 1]) / (lux_points[i] - lux_points[i - 1]);
            return brightness_points[i - 1] +
                   (int)(ratio * (brightness_points[i] - brightness_points[i - 1]) + 0.5f);
        }
    }
    return brightness_points[count - 1];
}

static void save_int_file(const char * path, int value) {
    std::ofstream file(path);
    if (file.is_open()) file << value;
}

static void set_auto_brightness_enabled(bool enabled) {
    auto_brightness_enabled = enabled;
    save_int_file("/tuya/data/ha_auto_brightness", enabled ? 1 : 0);
    if (!enabled) {
        auto_brightness_percent = manual_brightness_percent;
        int raw = manual_brightness_percent * backlight_max / 100;
        if (g_screen_blanked) g_active_backlight_raw = raw;
        else hal_set_backlight(raw);
    }
    if (display_auto_switch) {
        if (enabled) lv_obj_add_state(display_auto_switch, LV_STATE_CHECKED);
        else lv_obj_clear_state(display_auto_switch, LV_STATE_CHECKED);
    }
    if (display_brightness_slider) {
        if (enabled) lv_obj_add_state(display_brightness_slider, LV_STATE_DISABLED);
        else lv_obj_clear_state(display_brightness_slider, LV_STATE_DISABLED);
    }
}

static void auto_brightness_timer_cb(lv_timer_t * timer) {
    (void)timer;
    bool persisted_enabled = read_int_file("/tuya/data/ha_auto_brightness",
                                           auto_brightness_enabled ? 1 : 0) != 0;
    if (persisted_enabled != auto_brightness_enabled) {
        if (!persisted_enabled) {
            manual_brightness_percent = read_int_file("/tuya/data/ha_brightness",
                                                       manual_brightness_percent);
        }
        set_auto_brightness_enabled(persisted_enabled);
    }

    float lux = read_ambient_lux();
    if (lux < 0.0f) return;
    filtered_ambient_lux = filtered_ambient_lux < 0.0f ? lux : filtered_ambient_lux * 0.8f + lux * 0.2f;

    if (display_auto_status_label) {
        char status[48];
        snprintf(status, sizeof(status), "Światło otoczenia: %.0f lx", filtered_ambient_lux);
        lv_label_set_text(display_auto_status_label, status);
    }
    if (!auto_brightness_enabled) return;

    int target = ambient_lux_to_brightness(filtered_ambient_lux);
    int difference = target - auto_brightness_percent;
    if (difference >= -2 && difference <= 2) return;
    int step = difference > 0 ? 2 : -2;
    if ((difference > 0 && difference < step) || (difference < 0 && difference > step)) step = difference;
    auto_brightness_percent += step;

    int raw = auto_brightness_percent * backlight_max / 100;
    if (g_screen_blanked) g_active_backlight_raw = raw;
    else hal_set_backlight(raw);
    if (brightness_slider) lv_slider_set_value(brightness_slider, auto_brightness_percent, LV_ANIM_ON);
    if (brightness_value_label) set_percent_label(brightness_value_label, auto_brightness_percent);
    if (display_brightness_slider) lv_slider_set_value(display_brightness_slider, auto_brightness_percent, LV_ANIM_ON);
    if (display_brightness_label) set_percent_label(display_brightness_label, auto_brightness_percent);
}

static void display_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_display_screen();
}

static void display_brightness_event_cb(lv_event_t * e) {
    lv_obj_t * slider = lv_event_get_target(e);
    int percent = lv_slider_get_value(slider);
    set_percent_label(display_brightness_label, percent);
    if (auto_brightness_enabled) return;
    manual_brightness_percent = percent;
    hal_set_backlight(percent * backlight_max / 100);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED || lv_event_get_code(e) == LV_EVENT_PRESS_LOST) {
        save_int_file("/tuya/data/ha_brightness", manual_brightness_percent);
    }
}

static void display_auto_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    bool enabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    set_auto_brightness_enabled(enabled);
}

static void display_update_timeout_buttons(int selected_seconds) {
    for (int i = 0; i < 4; ++i) {
        if (!display_timeout_buttons[i]) continue;
        bool selected = display_timeout_values[i] == selected_seconds;
        lv_obj_set_style_bg_color(display_timeout_buttons[i],
                                  selected ? M3_PRIMARY_CONTAINER : M3_SURFACE_HIGH, LV_PART_MAIN);
        lv_obj_set_style_border_width(display_timeout_buttons[i], selected ? 0 : 1, LV_PART_MAIN);
        lv_obj_t * text = lv_obj_get_child(display_timeout_buttons[i], 0);
        lv_obj_set_style_text_color(text,
                                    selected ? M3_ON_PRIMARY_CONTAINER : M3_ON_SURFACE_VARIANT,
                                    LV_PART_MAIN);
    }
}

static void display_timeout_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int seconds = (int)(long)lv_event_get_user_data(e);
    screen_timeout_ms = seconds * 1000;
    std::ofstream timeout_file("/tuya/data/ha_screen_timeout");
    if (timeout_file.is_open()) timeout_file << seconds;
    display_update_timeout_buttons(seconds);
    lv_disp_trig_activity(NULL);
}

static void create_display_screen(void) {
    if (display_screen) return;

    lv_obj_t * list = NULL;
    display_screen = create_subscreen_base("Ekran", display_back_event_cb, &list);

    backlight_max = read_int_file("/sys/class/backlight/backlight/max_brightness", 255);
    if (backlight_max < 1) backlight_max = 255;
    int brightness = auto_brightness_enabled ? auto_brightness_percent : manual_brightness_percent;
    char hero_subtitle[32];
    char brightness_text[16];
    snprintf(hero_subtitle, sizeof(hero_subtitle), auto_brightness_enabled ? "Automatycznie • %d%%" : "Jasność %d%%",
             brightness);
    snprintf(brightness_text, sizeof(brightness_text), "%d%%", brightness);
    settings_m3_hero(list, "Ekran aktywny", hero_subtitle, ICON_BRIGHTNESS, true);

    lv_obj_t * auto_card = settings_m3_card(list, 20, 96, 440, 76);
    settings_m3_label(auto_card, "Automatyczna jasność", 16, 12, M3_ON_SURFACE,
                      &lv_font_montserrat_20_pl);
    char ambient_status[48];
    if (filtered_ambient_lux >= 0.0f) {
        snprintf(ambient_status, sizeof(ambient_status), "Światło otoczenia: %.0f lx", filtered_ambient_lux);
    } else {
        snprintf(ambient_status, sizeof(ambient_status), "Czujnik światła STK3310");
    }
    display_auto_status_label = settings_m3_label(auto_card, ambient_status, 16, 44,
                                                   M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    display_auto_switch = lv_switch_create(auto_card);
    settings_m3_style_switch(display_auto_switch);
    lv_obj_set_pos(display_auto_switch, 374, 24);
    if (auto_brightness_enabled) lv_obj_add_state(display_auto_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(display_auto_switch, display_auto_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * brightness_card = settings_m3_card(list, 20, 184, 440, 126);
    settings_m3_label(brightness_card, "Jasność ekranu", 16, 14, M3_ON_SURFACE,
                      &lv_font_montserrat_20_pl);
    display_brightness_label = settings_m3_label(brightness_card, brightness_text, 0, 16,
                                                  M3_PRIMARY, &lv_font_montserrat_20_pl);
    lv_obj_align(display_brightness_label, LV_ALIGN_TOP_RIGHT, -16, 16);
    display_brightness_slider = lv_slider_create(brightness_card);
    lv_obj_set_size(display_brightness_slider, 408, 20);
    lv_obj_set_pos(display_brightness_slider, 16, 77);
    lv_slider_set_range(display_brightness_slider, 5, 100);
    lv_slider_set_value(display_brightness_slider, brightness, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(display_brightness_slider, M3_OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(display_brightness_slider, M3_PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(display_brightness_slider, M3_ON_PRIMARY_CONTAINER, LV_PART_KNOB);
    lv_obj_set_style_bg_opa(display_brightness_slider, LV_OPA_50, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_add_event_cb(display_brightness_slider, display_brightness_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(display_brightness_slider, display_brightness_event_cb, LV_EVENT_RELEASED, NULL);
    if (auto_brightness_enabled) lv_obj_add_state(display_brightness_slider, LV_STATE_DISABLED);

    lv_obj_t * timeout_card = settings_m3_card(list, 20, 322, 440, 132);
    settings_m3_label(timeout_card, "Wygaszanie ekranu", 16, 14, M3_ON_SURFACE,
                      &lv_font_montserrat_20_pl);
    settings_m3_label(timeout_card, "Po czasie bezczynności", 16, 44, M3_ON_SURFACE_VARIANT,
                      &lv_font_montserrat_14);
    const char * timeout_labels[] = {"15 s", "30 s", "1 min", "Nigdy"};
    int current_timeout = screen_timeout_ms / 1000;
    for (int i = 0; i < 4; ++i) {
        bool selected = display_timeout_values[i] == current_timeout;
        display_timeout_buttons[i] = lv_btn_create(timeout_card);
        lv_obj_set_size(display_timeout_buttons[i], 94, 40);
        lv_obj_set_pos(display_timeout_buttons[i], 16 + i * 102, 78);
        settings_m3_surface(display_timeout_buttons[i],
                            selected ? M3_PRIMARY_CONTAINER : M3_SURFACE_HIGH, 20);
        lv_obj_set_style_border_color(display_timeout_buttons[i], M3_OUTLINE_VARIANT, LV_PART_MAIN);
        lv_obj_set_style_border_width(display_timeout_buttons[i], selected ? 0 : 1, LV_PART_MAIN);
        lv_obj_t * text = settings_m3_label(display_timeout_buttons[i], timeout_labels[i], 0, 0,
                                             selected ? M3_ON_PRIMARY_CONTAINER : M3_ON_SURFACE_VARIANT,
                                             &lv_font_montserrat_14);
        lv_obj_center(text);
        lv_obj_add_event_cb(display_timeout_buttons[i], display_timeout_event_cb,
                            LV_EVENT_CLICKED, (void *)(long)display_timeout_values[i]);
    }
    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 466);
    lv_obj_set_style_bg_opa(spacer, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(spacer, 0, LV_PART_MAIN);
}

static std::string exec_cmd_line(const char * cmd) {
    FILE * pipe = popen(cmd, "r");
    if (!pipe) return "";
    char buf[256];
    std::string result = "";
    if (fgets(buf, sizeof(buf), pipe)) result = buf;
    pclose(pipe);
    while (!result.empty() && (result.back() == '\n' || result.back() == '\r' || result.back() == ' ')) result.pop_back();
    return result;
}

static void wifi_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_wifi_screen();
}

static void wifi_update_status_header(void) {
    if (!wifi_status_label || !wifi_status_dot) return;
    std::string ip = get_wlan0_ip();
    bool connected = wifi_interface_enabled && (ip != "127.0.0.1" && !ip.empty());
    if (!wifi_interface_enabled) {
        lv_label_set_text(wifi_status_label, "Wi-Fi wyłączone");
        settings_m3_surface(wifi_status_dot, M3_OUTLINE, 4);
    } else if (connected) {
        lv_label_set_text(wifi_status_label, "Połączono");
        settings_m3_surface(wifi_status_dot, M3_SUCCESS, 4);
    } else {
        lv_label_set_text(wifi_status_label, "Rozłączono");
        settings_m3_surface(wifi_status_dot, M3_OUTLINE, 4);
    }
}

static void wifi_switch_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    wifi_interface_enabled = lv_obj_has_state(wifi_switch, LV_STATE_CHECKED);
    if (wifi_interface_enabled) {
        system("ifconfig wlan0 up 2>/dev/null");
    } else {
        system("ifconfig wlan0 down 2>/dev/null");
    }
    wifi_update_status_header();
}

static void wifi_disconnect_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    std::thread([]() {
        system("wpa_cli -p /var/run/wpa_supplicant -i wlan0 disconnect 2>/dev/null");
        system("wpa_cli -p /var/run/wpa_supplicant -i wlan0 disable_network all 2>/dev/null");
    }).detach();
    wifi_update_status_header();
}

static void wifi_close_modal_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t * modal = (lv_obj_t *)lv_event_get_user_data(e);
    if (modal) lv_obj_del_async(modal);
}

static void open_wifi_info_modal_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t * modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 480, 480);
    lv_obj_set_pos(modal, 0, 0);
    settings_m3_surface(modal, lv_color_make(0x11, 0x13, 0x18), 0);

    lv_obj_t * back = lv_btn_create(modal);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    settings_m3_surface(back, M3_SURFACE_HIGH, 24);
    lv_obj_add_event_cb(back, wifi_close_modal_cb, LV_EVENT_CLICKED, modal);
    lv_obj_t * back_icon = settings_m3_label(back, ICON_BACK, 0, 0, M3_ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    settings_m3_label(modal, "Szczegóły połączenia", 76, 22, M3_ON_SURFACE, &lv_font_montserrat_24_pl);

    lv_obj_t * list = lv_obj_create(modal);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    settings_m3_surface(list, lv_color_make(0x11, 0x13, 0x18), 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    std::string ip = get_wlan0_ip();
    std::string ssid = exec_cmd_line("wpa_cli -i wlan0 status 2>/dev/null | grep '^ssid=' | cut -d= -f2");
    if (ssid.empty()) ssid = (ip != "127.0.0.1" && !ip.empty()) ? "Połączono" : "Brak sieci";

    std::string rssi = exec_cmd_line("wpa_cli -i wlan0 signal_poll 2>/dev/null | grep '^RSSI=' | cut -d= -f2");
    std::string signal_str = !rssi.empty() ? (rssi + " dBm") : "Bardzo dobry (-58 dBm)";

    std::string mac = exec_cmd_line("cat /sys/class/net/wlan0/address 2>/dev/null");
    if (mac.empty()) mac = "8c:88:2b:00:07:14";

    std::string gateway = exec_cmd_line("ip route show dev wlan0 2>/dev/null | grep default | awk '{print $3}'");
    if (gateway.empty()) gateway = "192.168.1.1";

    std::string dns = exec_cmd_line("grep nameserver /etc/resolv.conf 2>/dev/null | awk '{print $2}' | tr '\\n' ' '");
    if (dns.empty()) dns = "8.8.8.8  4.2.2.2";

    const char * keys[] = {
        "SSID / SIEĆ", "SIŁA SYGNAŁU", "ZABEZPIECZENIA", "ADRES MAC",
        "ADRES IPV4", "MASKA PODSIECI", "BRAMA (GATEWAY)", "SERWERY DNS", "INTERFEJS"
    };
    std::string vals[] = {
        ssid, signal_str, "WPA2-PSK (AES)", mac,
        ip, "255.255.255.0", gateway, dns, "wlan0"
    };

    for (int i = 0; i < 9; ++i) {
        int y = 8 + i * 72;
        lv_obj_t * item = settings_m3_card(list, 20, y, 440, 64);
        settings_m3_label(item, keys[i], 16, 10, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
        settings_m3_label(item, vals[i].c_str(), 16, 34, M3_ON_SURFACE, &lv_font_montserrat_16_pl);
    }

    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 656);
    settings_m3_surface(spacer, lv_color_make(0x11, 0x13, 0x18), 0);
}

static void open_ip_modal_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    lv_obj_t * modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 480, 480);
    lv_obj_set_pos(modal, 0, 0);
    settings_m3_surface(modal, lv_color_make(0x11, 0x13, 0x18), 0);

    lv_obj_t * back = lv_btn_create(modal);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    settings_m3_surface(back, M3_SURFACE_HIGH, 24);
    lv_obj_add_event_cb(back, wifi_close_modal_cb, LV_EVENT_CLICKED, modal);
    lv_obj_t * back_icon = settings_m3_label(back, ICON_BACK, 0, 0, M3_ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    settings_m3_label(modal, "Ustawienia IP", 76, 22, M3_ON_SURFACE, &lv_font_montserrat_24_pl);

    lv_obj_t * list = lv_obj_create(modal);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    settings_m3_surface(list, lv_color_make(0x11, 0x13, 0x18), 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * mode_card = settings_m3_card(list, 20, 8, 440, 72);
    settings_m3_label(mode_card, "Tryb konfiguracyjny", 16, 12, M3_ON_SURFACE, &lv_font_montserrat_16_pl);
    settings_m3_label(mode_card, wifi_static_ip_mode ? "Statyczny adres IP" : "Automatyczny (DHCP)", 16, 40, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);

    lv_obj_t * btn_dhcp = lv_btn_create(mode_card);
    lv_obj_set_size(btn_dhcp, 140, 40);
    lv_obj_set_pos(btn_dhcp, 284, 16);
    settings_m3_surface(btn_dhcp, wifi_static_ip_mode ? M3_SURFACE_HIGH : M3_PRIMARY_CONTAINER, 20);
    lv_obj_t * lbl_dhcp = settings_m3_label(btn_dhcp, wifi_static_ip_mode ? "Użyj DHCP" : "DHCP ✓", 0, 0, wifi_static_ip_mode ? M3_ON_SURFACE_VARIANT : M3_ON_PRIMARY_CONTAINER, &lv_font_montserrat_14_pl);
    lv_obj_center(lbl_dhcp);

    std::string ip = get_wlan0_ip();
    settings_m3_text_field(list, "Adres IPv4", ip.c_str(), "192.168.1.x", 20, 92, 440, false);
    settings_m3_text_field(list, "Maska podsieci", "255.255.255.0", "255.255.255.0", 20, 168, 440, false);
    settings_m3_text_field(list, "Brama domyślna", "192.168.1.1", "192.168.1.1", 20, 244, 440, false);
    settings_m3_text_field(list, "Główny DNS", "8.8.8.8", "8.8.8.8", 20, 320, 212, false);
    settings_m3_text_field(list, "Zapasowy DNS", "4.2.2.2", "4.2.2.2", 248, 320, 212, false);

    settings_m3_button(list, "Zapisz ustawienia", 244, 400, 216, wifi_close_modal_cb);

    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 458);
    settings_m3_surface(spacer, lv_color_make(0x11, 0x13, 0x18), 0);
}

struct WifiNetworkInfo {
    std::string ssid;
    std::string bssid;
    int rssi;
    std::string flags;
};

struct WifiConnectData {
    std::string ssid;
    lv_obj_t * modal;
    lv_obj_t * ta_pass;
};

static std::string unescape_wpa_ssid(const std::string & input) {
    std::string out;
    out.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i] == '\\' && i + 3 < input.size() && input[i+1] == 'x') {
            char hex[3] = { input[i+2], input[i+3], '\0' };
            char * end = NULL;
            long val = strtol(hex, &end, 16);
            if (end == hex + 2) {
                out.push_back((char)val);
                i += 3;
                continue;
            }
        }
        out.push_back(input[i]);
    }
    return out;
}

static void wifi_connect_action_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    WifiConnectData * data = (WifiConnectData *)lv_event_get_user_data(e);
    if (!data) return;

    std::string ssid = data->ssid;
    std::string pass = data->ta_pass ? lv_textarea_get_text(data->ta_pass) : "";
    lv_obj_t * modal = data->modal;

    std::thread([ssid, pass]() {
        std::string cmd_add = exec_cmd_line("wpa_cli -i wlan0 add_network 2>/dev/null | tail -n 1");
        int net_id = atoi(cmd_add.c_str());

        char cmd[512];
        snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 set_network %d ssid '\"%s\"'", net_id, ssid.c_str());
        system(cmd);

        if (!pass.empty()) {
            snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 set_network %d psk '\"%s\"'", net_id, pass.c_str());
        } else {
            snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 set_network %d key_mgmt NONE", net_id);
        }
        system(cmd);

        snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 enable_network %d", net_id);
        system(cmd);
        snprintf(cmd, sizeof(cmd), "wpa_cli -i wlan0 select_network %d", net_id);
        system(cmd);
        system("wpa_cli -i wlan0 save_config 2>/dev/null");
        system("udhcpc -i wlan0 -q -n -t 5 2>/dev/null &");
    }).detach();

    if (modal) lv_obj_del_async(modal);
    delete data;
}

static void open_wifi_connect_modal(const std::string & target_ssid) {
    lv_obj_t * modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(modal, 480, 480);
    lv_obj_set_pos(modal, 0, 0);
    settings_m3_surface(modal, lv_color_make(0x11, 0x13, 0x18), 0);

    lv_obj_t * back = lv_btn_create(modal);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    settings_m3_surface(back, M3_SURFACE_HIGH, 24);
    lv_obj_add_event_cb(back, wifi_close_modal_cb, LV_EVENT_CLICKED, modal);
    lv_obj_t * back_icon = settings_m3_label(back, ICON_BACK, 0, 0, M3_ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    settings_m3_label(modal, "Połącz z siecią", 76, 22, M3_ON_SURFACE, &lv_font_montserrat_24_pl);

    lv_obj_t * card = settings_m3_card(modal, 20, 76, 440, 150);
    settings_m3_label(card, "SIEĆ WI-FI", 16, 12, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    settings_m3_label(card, target_ssid.c_str(), 16, 30, M3_ON_SURFACE, &lv_font_montserrat_20_pl);

    settings_m3_label(card, "HASŁO DO SIECI", 16, 68, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_t * ta_pass = lv_textarea_create(card);
    lv_obj_set_size(ta_pass, 408, 44);
    lv_obj_set_pos(ta_pass, 16, 92);
    lv_textarea_set_one_line(ta_pass, true);
    lv_textarea_set_password_mode(ta_pass, true);
    lv_textarea_set_placeholder_text(ta_pass, "Wpisz hasło Wi-Fi...");
    lv_obj_set_style_text_font(ta_pass, &lv_font_montserrat_16_pl, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta_pass, M3_SURFACE_HIGH, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta_pass, M3_OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_radius(ta_pass, 12, LV_PART_MAIN);

    lv_obj_t * kb = lv_keyboard_create(modal);
    lv_obj_set_size(kb, 480, 180);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, -52);
    lv_keyboard_set_textarea(kb, ta_pass);

    WifiConnectData * conn_data = new WifiConnectData{target_ssid, modal, ta_pass};

    lv_obj_t * connect_btn = lv_btn_create(modal);
    lv_obj_set_size(connect_btn, 212, 44);
    lv_obj_set_pos(connect_btn, 248, 428);
    settings_m3_surface(connect_btn, M3_PRIMARY, 22);
    lv_obj_t * conn_lbl = settings_m3_label(connect_btn, "Połącz", 0, 0, M3_ON_PRIMARY, &lv_font_montserrat_16_pl);
    lv_obj_center(conn_lbl);
    lv_obj_add_event_cb(connect_btn, wifi_connect_action_cb, LV_EVENT_CLICKED, conn_data);

    lv_obj_t * cancel_btn = lv_btn_create(modal);
    lv_obj_set_size(cancel_btn, 212, 44);
    lv_obj_set_pos(cancel_btn, 20, 428);
    settings_m3_surface(cancel_btn, M3_SURFACE_HIGH, 22);
    lv_obj_t * canc_lbl = settings_m3_label(cancel_btn, "Anuluj", 0, 0, M3_ON_SURFACE, &lv_font_montserrat_16_pl);
    lv_obj_center(canc_lbl);
    lv_obj_add_event_cb(cancel_btn, wifi_close_modal_cb, LV_EVENT_CLICKED, modal);
}

static void wifi_connect_item_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char * ssid = (const char *)lv_event_get_user_data(e);
    if (ssid) {
        open_wifi_connect_modal(std::string(ssid));
    }
}

static void render_wifi_scan_results_timer_cb(lv_timer_t * timer) {
    lv_timer_del(timer);

    if (wifi_scan_btn_label) {
        lv_label_set_text(wifi_scan_btn_label, "Odśwież listę sieci Wi-Fi");
    }

    if (!wifi_avail_container) return;

    lv_obj_clean(wifi_avail_container);

    std::string current_ssid = exec_cmd_line("wpa_cli -i wlan0 status 2>/dev/null | grep '^ssid=' | cut -d= -f2");

    std::string raw = exec_cmd_line("wpa_cli -i wlan0 scan_results 2>/dev/null");
    std::stringstream ss(raw);
    std::string line;
    bool first = true;
    std::map<std::string, WifiNetworkInfo> unique_ssids;

    while (std::getline(ss, line)) {
        if (first) { first = false; continue; }
        if (line.empty()) continue;

        std::vector<std::string> tokens;
        std::stringstream line_ss(line);
        std::string token;
        while (std::getline(line_ss, token, '\t')) {
            tokens.push_back(token);
        }

        if (tokens.size() >= 5) {
            std::string bssid = tokens[0];
            int rssi = atoi(tokens[2].c_str());
            std::string flags = tokens[3];
            std::string raw_ssid = tokens[4];

            while (!raw_ssid.empty() && (raw_ssid.back() == '\r' || raw_ssid.back() == '\n')) raw_ssid.pop_back();
            std::string ssid = unescape_wpa_ssid(raw_ssid);

            if (ssid.empty()) continue;

            if (unique_ssids.find(ssid) == unique_ssids.end() || rssi > unique_ssids[ssid].rssi) {
                WifiNetworkInfo net;
                net.ssid = ssid;
                net.bssid = bssid;
                net.rssi = rssi;
                net.flags = flags;
                unique_ssids[ssid] = net;
            }
        }
    }

    std::vector<WifiNetworkInfo> networks;
    for (auto const & pair : unique_ssids) {
        if (pair.first != current_ssid) {
            networks.push_back(pair.second);
        }
    }

    std::sort(networks.begin(), networks.end(), [](const WifiNetworkInfo & a, const WifiNetworkInfo & b) {
        return a.rssi > b.rssi;
    });

    int y = 0;
    if (networks.empty()) {
        lv_obj_t * empty_card = settings_m3_card(wifi_avail_container, 20, y, 440, 56);
        settings_m3_label(empty_card, "Brak dodatkowych sieci w zasięgu", 16, 18, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
        y += 64;
    } else {
        for (size_t i = 0; i < networks.size(); ++i) {
            const auto & net = networks[i];
            lv_obj_t * item = settings_m3_card(wifi_avail_container, 20, y, 440, 68);

            lv_obj_t * name_lbl = settings_m3_label(item, net.ssid.c_str(), 16, 12, M3_ON_SURFACE, &lv_font_montserrat_16_pl);
            lv_obj_set_width(name_lbl, 260);

            std::string signal_desc;
            if (net.rssi >= -50) signal_desc = "Sygnał: Bardzo silny (" + std::to_string(net.rssi) + " dBm)";
            else if (net.rssi >= -65) signal_desc = "Sygnał: Dobry (" + std::to_string(net.rssi) + " dBm)";
            else if (net.rssi >= -80) signal_desc = "Sygnał: Dostateczny (" + std::to_string(net.rssi) + " dBm)";
            else signal_desc = "Sygnał: Słaby (" + std::to_string(net.rssi) + " dBm)";

            settings_m3_label(item, signal_desc.c_str(), 16, 38, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);

            lv_obj_t * conn_btn = lv_btn_create(item);
            lv_obj_set_size(conn_btn, 92, 36);
            lv_obj_set_pos(conn_btn, 332, 16);
            settings_m3_surface(conn_btn, M3_SURFACE_HIGH, 18);
            lv_obj_set_style_border_color(conn_btn, M3_OUTLINE_VARIANT, LV_PART_MAIN);
            lv_obj_set_style_border_width(conn_btn, 1, LV_PART_MAIN);

            lv_obj_t * lbl = settings_m3_label(conn_btn, "Połącz", 0, 0, M3_PRIMARY, &lv_font_montserrat_14_pl);
            lv_obj_center(lbl);

            char * ssid_copy = strdup(net.ssid.c_str());
            lv_obj_add_event_cb(conn_btn, wifi_connect_item_btn_cb, LV_EVENT_CLICKED, ssid_copy);

            y += 76;
        }
    }

    lv_obj_set_height(wifi_avail_container, y + 10);
}

static void wifi_scan_refresh_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;

    if (wifi_scan_btn_label) {
        lv_label_set_text(wifi_scan_btn_label, "Skanowanie sieci...");
    }

    system("wpa_cli -i wlan0 scan 2>/dev/null &");
    lv_timer_create(render_wifi_scan_results_timer_cb, 1200, NULL);
}

static void create_wifi_screen(void) {
    if (wifi_screen) return;

    lv_obj_t * list = NULL;
    wifi_screen = create_subscreen_base("Wi-Fi", wifi_back_event_cb, &list);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    std::string ip = get_wlan0_ip();
    bool connected = (ip != "127.0.0.1" && !ip.empty());

    // 1. Hero Card: Status ONLY (Połączono / Rozłączono / Wyłączone)
    lv_obj_t * hero = settings_m3_card(list, 20, 8, 440, 76);
    lv_obj_set_style_bg_color(hero, M3_PRIMARY_CONTAINER, LV_PART_MAIN);

    wifi_status_dot = lv_obj_create(hero);
    lv_obj_set_size(wifi_status_dot, 8, 8);
    lv_obj_set_pos(wifi_status_dot, 16, 34);
    settings_m3_surface(wifi_status_dot, connected ? M3_SUCCESS : M3_OUTLINE, 4);

    wifi_status_label = settings_m3_label(hero, connected ? "Połączono" : "Rozłączono", 34, 24, M3_ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    icon_badge(hero, ICON_WIFI, 380, 16, lv_color_hex(0x116872), M3_ON_PRIMARY_CONTAINER);

    // 2. Wi-Fi Switch Card: ONLY "Wi-Fi" label
    lv_obj_t * toggle_card = settings_m3_card(list, 20, 96, 440, 68);
    settings_m3_label(toggle_card, "Wi-Fi", 16, 22, M3_ON_SURFACE, &lv_font_montserrat_20_pl);

    wifi_switch = lv_switch_create(toggle_card);
    settings_m3_style_switch(wifi_switch);
    lv_obj_set_pos(wifi_switch, 374, 20);
    lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(wifi_switch, wifi_switch_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // 3. Side-By-Side Action Cards: [Więcej informacji] [Ustawienia IP]
    lv_obj_t * info_btn = settings_m3_card(list, 20, 176, 212, 72);
    lv_obj_add_flag(info_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(info_btn, M3_SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    settings_m3_label(info_btn, "Więcej informacji", 14, 14, M3_ON_SURFACE, &lv_font_montserrat_16_pl);
    settings_m3_label(info_btn, "Sygnał, MAC, IP", 14, 42, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_add_event_cb(info_btn, open_wifi_info_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * ip_btn = settings_m3_card(list, 248, 176, 212, 72);
    lv_obj_add_flag(ip_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ip_btn, M3_SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    settings_m3_label(ip_btn, "Ustawienia IP", 14, 14, M3_ON_SURFACE, &lv_font_montserrat_16_pl);
    settings_m3_label(ip_btn, "DHCP / Statyczny IP", 14, 42, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_add_event_cb(ip_btn, open_ip_modal_cb, LV_EVENT_CLICKED, NULL);

    // 4. Active Connection Item
    int y = 260;
    lv_obj_t * sec_hdr = settings_m3_label(list, "POŁĄCZONA SIEĆ", 24, y, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_set_style_text_letter_space(sec_hdr, 1, LV_PART_MAIN);
    y += 24;

    std::string ssid = exec_cmd_line("wpa_cli -i wlan0 status 2>/dev/null | grep '^ssid=' | cut -d= -f2");
    if (ssid.empty()) ssid = connected ? "Aktywna sieć Wi-Fi" : "Brak połączenia";

    lv_obj_t * item = settings_m3_card(list, 20, y, 440, 68);
    if (connected) lv_obj_set_style_bg_color(item, M3_PRIMARY_CONTAINER, LV_PART_MAIN);

    lv_obj_t * ssid_lbl = settings_m3_label(item, ssid.c_str(), 16, 12, connected ? M3_ON_PRIMARY_CONTAINER : M3_ON_SURFACE, &lv_font_montserrat_16_pl);
    lv_obj_set_width(ssid_lbl, 260);

    settings_m3_label(item, connected ? "Sygnał: Bardzo dobry • WPA2/WPA3" : "Brak połączenia z siecią", 16, 38, connected ? lv_color_hex(0x8BD7DF) : M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);

    lv_obj_t * btn = lv_btn_create(item);
    lv_obj_set_size(btn, 92, 36);
    lv_obj_set_pos(btn, 332, 16);
    if (connected) {
        settings_m3_surface(btn, lv_color_hex(0x93000A), 18);
        lv_obj_t * lbl = settings_m3_label(btn, "Rozłącz", 0, 0, lv_color_hex(0xFFB4AB), &lv_font_montserrat_14_pl);
        lv_obj_center(lbl);
        lv_obj_add_event_cb(btn, wifi_disconnect_btn_cb, LV_EVENT_CLICKED, NULL);
    } else {
        settings_m3_surface(btn, M3_SURFACE_HIGH, 18);
        lv_obj_set_style_border_color(btn, M3_OUTLINE_VARIANT, LV_PART_MAIN);
        lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        lv_obj_t * lbl = settings_m3_label(btn, "Połącz", 0, 0, M3_PRIMARY, &lv_font_montserrat_14_pl);
        lv_obj_center(lbl);
    }

    y += 84;
    lv_obj_t * avail_hdr = settings_m3_label(list, "DOSTĘPNE SIECI", 24, y, M3_ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_set_style_text_letter_space(avail_hdr, 1, LV_PART_MAIN);

    y += 24;
    lv_obj_t * scan_card = settings_m3_card(list, 20, y, 440, 52);
    lv_obj_add_flag(scan_card, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(scan_card, M3_SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    wifi_scan_btn_label = settings_m3_label(scan_card, "Skanowanie sieci...", 16, 16, M3_PRIMARY, &lv_font_montserrat_16_pl);
    icon_badge(scan_card, ICON_REFRESH, 388, 10, lv_color_hex(0x1F2A30), M3_PRIMARY);
    lv_obj_add_event_cb(scan_card, wifi_scan_refresh_btn_cb, LV_EVENT_CLICKED, NULL);

    y += 60;
    wifi_avail_container = lv_obj_create(list);
    lv_obj_set_size(wifi_avail_container, 480, 100);
    lv_obj_set_pos(wifi_avail_container, 0, y);
    settings_m3_surface(wifi_avail_container, lv_color_make(0x11, 0x13, 0x18), 0);
    lv_obj_clear_flag(wifi_avail_container, LV_OBJ_FLAG_SCROLLABLE);

    // Initial async scan on opening Wi-Fi screen
    system("wpa_cli -i wlan0 scan 2>/dev/null &");
    lv_timer_create(render_wifi_scan_results_timer_cb, 1200, NULL);
}

static lv_obj_t * web_portal_screen = NULL;
static lv_obj_t * web_portal_status_dot = NULL;
static lv_obj_t * web_portal_status_label = NULL;
static lv_obj_t * web_portal_server_switch = NULL;
static lv_obj_t * web_portal_restart_button = NULL;
static lv_obj_t * web_portal_restart_label = NULL;
static lv_obj_t * web_portal_snackbar = NULL;

static void web_portal_make_surface(lv_obj_t * obj, lv_color_t color, int radius) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t * web_portal_make_label(lv_obj_t * parent, const char * text, int x, int y,
                                        lv_color_t color, const lv_font_t * font) {
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

static void web_portal_style_switch(lv_obj_t * sw) {
    lv_obj_set_size(sw, 48, 28);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x282A2F), LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, lv_color_hex(0x8D9199), LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x8D9199), LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x4FD8E6), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, lv_color_hex(0x00363D), LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN | LV_STATE_CHECKED);
}

static void web_portal_update_state(void) {
    if (!web_portal_status_label || !web_portal_server_switch) return;
    bool active = (system("pidof httpd >/dev/null") == 0);
    lv_label_set_text(web_portal_status_label, active ? "Serwer włączony" : "Serwer wyłączony");
    lv_obj_set_style_bg_color(web_portal_status_dot,
                              active ? lv_color_hex(0x91D18B) : lv_color_hex(0x8D9199), LV_PART_MAIN);
    if (active) {
        lv_obj_add_state(web_portal_server_switch, LV_STATE_CHECKED);
        lv_obj_clear_state(web_portal_restart_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(web_portal_server_switch, LV_STATE_CHECKED);
        lv_obj_add_state(web_portal_restart_button, LV_STATE_DISABLED);
    }
}

static void close_web_portal_screen(void) {
    if (web_portal_screen) {
        lv_obj_del_async(web_portal_screen);
        web_portal_screen = NULL;
        web_portal_status_dot = NULL;
        web_portal_status_label = NULL;
        web_portal_server_switch = NULL;
        web_portal_restart_button = NULL;
        web_portal_restart_label = NULL;
        web_portal_snackbar = NULL;
    }
}

static void web_portal_back_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) == LV_EVENT_CLICKED) close_web_portal_screen();
}

static void web_portal_manual_sw_cb(lv_event_t * e) {
    lv_obj_t * sw = lv_event_get_target(e);
    bool is_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    if (is_on) {
        if (system("pidof httpd >/dev/null") != 0) {
            system("iptables -I INPUT -p tcp --dport 80 -j ACCEPT 2>/dev/null");
            system("chmod +x /tuya/data/www/cgi-bin/* 2>/dev/null");
            system("httpd -h /tuya/data/www -p 80 &");
        }
        printf("[WebPortal] Manual HTTP server started.\n");
    } else {
        system("killall -9 httpd 2>/dev/null");
        printf("[WebPortal] Manual HTTP server stopped.\n");
    }
    web_portal_update_state();
}

static void web_portal_autostart_sw_cb(lv_event_t * e) {
    lv_obj_t * sw = lv_event_get_target(e);
    g_web_autostart = lv_obj_has_state(sw, LV_STATE_CHECKED);
    save_configuration();
    printf("[WebPortal] Auto-start setting saved: %d\n", g_web_autostart);
}

static void web_portal_hide_snackbar_cb(lv_timer_t * timer) {
    if (web_portal_snackbar) lv_obj_add_flag(web_portal_snackbar, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(timer);
}

static void web_portal_execute_restart_cb(lv_timer_t * timer) {
    system("killall -9 httpd 2>/dev/null; iptables -I INPUT -p tcp --dport 80 -j ACCEPT 2>/dev/null; chmod +x /tuya/data/www/cgi-bin/* 2>/dev/null; httpd -h /tuya/data/www -p 80 &");
    printf("[WebPortal] HTTP server restarted.\n");
    if (web_portal_restart_label) lv_label_set_text(web_portal_restart_label, "Restart serwera");
    web_portal_update_state();
    if (web_portal_snackbar) {
        lv_obj_clear_flag(web_portal_snackbar, LV_OBJ_FLAG_HIDDEN);
        lv_timer_create(web_portal_hide_snackbar_cb, 2100, NULL);
    }
    lv_timer_del(timer);
}

static void web_portal_restart_btn_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_label_set_text(web_portal_restart_label, "Restartowanie...");
    lv_obj_add_state(web_portal_restart_button, LV_STATE_DISABLED);
    lv_timer_create(web_portal_execute_restart_cb, 120, NULL);
}

static void create_web_portal_screen(void) {
    if (web_portal_screen) return;

    lv_obj_t * list = NULL;
    web_portal_screen = create_subscreen_base("Portal WWW", web_portal_back_event_cb, &list);

    std::string ip = get_wlan0_ip();
    bool httpd_active = (system("pidof httpd >/dev/null") == 0);

    lv_obj_clear_flag(list, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * status = lv_obj_create(list);
    lv_obj_set_size(status, 440, 76);
    lv_obj_set_pos(status, 20, 0);
    web_portal_make_surface(status, lv_color_hex(0x004F58), 28);

    web_portal_status_dot = lv_obj_create(status);
    lv_obj_set_size(web_portal_status_dot, 8, 8);
    lv_obj_set_pos(web_portal_status_dot, 16, 22);
    web_portal_make_surface(web_portal_status_dot,
                            httpd_active ? lv_color_hex(0x91D18B) : lv_color_hex(0x8D9199), 4);

    web_portal_status_label = web_portal_make_label(status,
        httpd_active ? "Serwer włączony" : "Serwer wyłączony",
        34, 10, lv_color_hex(0x9CF0FA), &lv_font_montserrat_20_pl);
    web_portal_make_label(status, ip.c_str(), 16, 43, lv_color_hex(0x8BD7DF), &lv_font_montserrat_14);

    lv_obj_t * globe_bg = lv_obj_create(status);
    lv_obj_set_size(globe_bg, 44, 44);
    lv_obj_set_pos(globe_bg, 380, 16);
    web_portal_make_surface(globe_bg, lv_color_hex(0x116872), 22);
    lv_obj_t * globe = web_portal_make_label(globe_bg, ICON_GLOBE, 0, 0,
                                              lv_color_hex(0x9CF0FA), &lv_font_control_icons_24);
    lv_obj_center(globe);

    lv_obj_t * controls = lv_obj_create(list);
    lv_obj_set_size(controls, 208, 306);
    lv_obj_set_pos(controls, 20, 88);
    web_portal_make_surface(controls, lv_color_hex(0x1D2024), 28);

    lv_obj_t * overline = web_portal_make_label(controls, "STEROWANIE", 16, 17,
                                                 lv_color_hex(0xC3C6CF), &lv_font_montserrat_14);
    lv_obj_set_style_text_letter_space(overline, 1, LV_PART_MAIN);

    lv_obj_t * server_label = web_portal_make_label(controls, "Serwer\nWWW", 16, 49,
                                                     lv_color_hex(0xE2E2E8), &lv_font_montserrat_20_pl);
    lv_obj_set_width(server_label, 105);
    lv_obj_set_style_text_line_space(server_label, 1, LV_PART_MAIN);

    web_portal_server_switch = lv_switch_create(controls);
    web_portal_style_switch(web_portal_server_switch);
    lv_obj_set_pos(web_portal_server_switch, 144, 54);
    if (httpd_active) lv_obj_add_state(web_portal_server_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(web_portal_server_switch, web_portal_manual_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t * divider = lv_obj_create(controls);
    lv_obj_set_size(divider, 176, 1);
    lv_obj_set_pos(divider, 16, 119);
    web_portal_make_surface(divider, lv_color_hex(0x43474E), 0);

    lv_obj_t * auto_label = web_portal_make_label(controls, "Autostart\nserwera", 16, 132,
                                                   lv_color_hex(0xE2E2E8), &lv_font_montserrat_20_pl);
    lv_obj_set_width(auto_label, 105);
    lv_obj_set_style_text_line_space(auto_label, 1, LV_PART_MAIN);

    lv_obj_t * auto_switch = lv_switch_create(controls);
    web_portal_style_switch(auto_switch);
    lv_obj_set_pos(auto_switch, 144, 140);
    if (g_web_autostart) lv_obj_add_state(auto_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(auto_switch, web_portal_autostart_sw_cb, LV_EVENT_VALUE_CHANGED, NULL);

    web_portal_restart_button = lv_btn_create(controls);
    lv_obj_set_size(web_portal_restart_button, 176, 48);
    lv_obj_set_pos(web_portal_restart_button, 16, 238);
    web_portal_make_surface(web_portal_restart_button, lv_color_hex(0x282A2F), 24);
    lv_obj_set_style_bg_color(web_portal_restart_button, lv_color_hex(0x343B40), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(web_portal_restart_button, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_add_event_cb(web_portal_restart_button, web_portal_restart_btn_cb, LV_EVENT_CLICKED, NULL);
    web_portal_restart_label = web_portal_make_label(web_portal_restart_button, "Restart serwera", 0, 0,
                                                      lv_color_hex(0x4FD8E6), &lv_font_montserrat_20_pl);
    lv_obj_center(web_portal_restart_label);

    lv_obj_t * qr_card = lv_obj_create(list);
    lv_obj_set_size(qr_card, 220, 306);
    lv_obj_set_pos(qr_card, 240, 88);
    web_portal_make_surface(qr_card, lv_color_hex(0x1D2024), 28);

    std::string qr_url = "http://" + ip + "/";
    lv_obj_t * qr = lv_qrcode_create(qr_card, 150, lv_color_black(), lv_color_white());
    lv_qrcode_update(qr, qr_url.c_str(), qr_url.length());
    lv_obj_set_pos(qr, 35, 24);

    lv_obj_t * qr_title = web_portal_make_label(qr_card, "Zeskanuj kod QR i\nprzejdź do panelu", 0, 0,
                                                 lv_color_hex(0xE2E2E8), &lv_font_montserrat_20_pl);
    lv_obj_set_style_text_align(qr_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(qr_title, LV_ALIGN_TOP_MID, 0, 192);
    lv_obj_t * qr_url_label = web_portal_make_label(qr_card, qr_url.c_str(), 0, 0,
                                                     lv_color_hex(0xC3C6CF), &lv_font_montserrat_14);
    lv_obj_align(qr_url_label, LV_ALIGN_TOP_MID, 0, 258);

    web_portal_snackbar = lv_obj_create(web_portal_screen);
    lv_obj_set_size(web_portal_snackbar, 440, 48);
    lv_obj_set_pos(web_portal_snackbar, 20, 416);
    web_portal_make_surface(web_portal_snackbar, lv_color_hex(0xE2E2E8), 8);
    web_portal_make_label(web_portal_snackbar, "Serwer został uruchomiony ponownie", 16, 15,
                           lv_color_hex(0x111318), &lv_font_montserrat_16_pl);
    lv_obj_add_flag(web_portal_snackbar, LV_OBJ_FLAG_HIDDEN);

    web_portal_update_state();
}

static void add_settings_card_switch(lv_obj_t * list, int * y, const char * icon_symbol,
                                     lv_color_t icon_color, const char * title,
                                     const char * subtitle, bool is_on,
                                     lv_event_cb_t event_cb) {
    lv_obj_t * card = lv_obj_create(list);
    lv_obj_set_size(card, 424, 78);
    lv_obj_set_pos(card, 28, *y);
    lv_obj_set_style_bg_color(card, lv_color_make(0x20, 0x23, 0x2B), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 0, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

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

    lv_obj_t * sw = lv_switch_create(card);
    lv_obj_set_size(sw, 50, 26);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -16, 0);
    if (is_on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_set_style_bg_color(sw, lv_color_make(0x3B, 0x40, 0x4E), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, lv_color_make(0x18, 0x65, 0xA8), LV_PART_MAIN | LV_STATE_CHECKED);
    if (event_cb) {
        lv_obj_add_event_cb(sw, event_cb, LV_EVENT_VALUE_CHANGED, subtitle_label);
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

    lv_obj_t * list = lv_obj_create(settings_screen);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(list, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(list, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(list, 0, LV_PART_MAIN);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t * header_bar = lv_obj_create(settings_screen);
    lv_obj_set_size(header_bar, 480, 70);
    lv_obj_set_pos(header_bar, 0, 0);
    lv_obj_set_style_bg_color(header_bar, lv_color_make(0x11, 0x13, 0x18), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(header_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(header_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(header_bar, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(header_bar, 0, LV_PART_MAIN);
    lv_obj_clear_flag(header_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * back = lv_btn_create(header_bar);
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

    lv_obj_t * heading = lv_label_create(header_bar);
    lv_label_set_text(heading, "Ustawienia");
    lv_obj_set_pos(heading, 76, 22);
    lv_obj_set_style_text_font(heading, &lv_font_montserrat_24, LV_PART_MAIN);
    lv_obj_set_style_text_color(heading, lv_color_make(0xE4, 0xE2, 0xE9), LV_PART_MAIN);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    int y = 8;
    std::string ip = get_wlan0_ip();
    std::string wifi_status = ip == "127.0.0.1" ? "Brak połączenia" : "Połączono - " + ip;
    std::string ha_status = config_exists() ? "Skonfigurowano" : "Wymaga konfiguracji";

    add_settings_section(list, "ŁĄCZNOŚĆ", &y);
    add_settings_card(list, &y, ICON_WIFI, lv_color_make(0x18, 0x65, 0xA8),
                      "Wi-Fi", wifi_status.c_str(), SETTINGS_ACTION_WIFI);
    add_settings_card(list, &y, ICON_BLUETOOTH, lv_color_make(0x4F, 0x5D, 0xB8),
                      "Bluetooth Proxy", "Planowane", SETTINGS_ACTION_NONE);
    add_settings_card(list, &y, ICON_PLUG, lv_color_make(0x38, 0x6A, 0x20),
                      "Zigbee Router", "Planowane", SETTINGS_ACTION_NONE);

    add_settings_section(list, "PANEL", &y);
    add_settings_card(list, &y, ICON_BRIGHTNESS, lv_color_make(0x9A, 0x56, 0x00),
                      "Ekran", "Jasność i wygaszanie", SETTINGS_ACTION_DISPLAY);
    add_settings_card(list, &y, ICON_VOLUME, lv_color_make(0x7A, 0x48, 0x92),
                      "Dźwięk", "Głośność i mikrofon", SETTINGS_ACTION_CONTROLS);
    add_settings_card(list, &y, ICON_PALETTE, lv_color_make(0x8C, 0x43, 0x53),
                      "Wygląd", "Motyw i ekran główny - planowane", SETTINGS_ACTION_NONE);

    add_settings_section(list, "HOME ASSISTANT", &y);
    add_settings_card(list, &y, ICON_HOME, lv_color_make(0x03, 0x78, 0xA6),
                      "Połączenie", ha_status.c_str(), SETTINGS_ACTION_NONE);

    bool httpd_active = (system("pidof httpd >/dev/null") == 0);
    std::string web_status = httpd_active ? (g_web_autostart ? "Włączony (Autostart)" : "Włączony (Ręcznie)") : "Wyłączony";
    add_settings_card(list, &y, ICON_GLOBE, lv_color_make(0x00, 0x68, 0x74),
                      "Portal WWW", web_status.c_str(), SETTINGS_ACTION_WEB_PORTAL);

    std::string mqtt_status = g_mqtt_config.enabled ? (g_mqtt_client.is_connected() ? "Połączono (" + g_mqtt_config.host + ")" : "Włączono (" + g_mqtt_config.host + ")") : "Wyłączony";
    add_settings_card(list, &y, ICON_PLUG, lv_color_make(0x00, 0x89, 0x7B),
                      "MQTT", mqtt_status.c_str(), SETTINGS_ACTION_MQTT);

    add_settings_card(list, &y, ICON_MIC, lv_color_make(0x6B, 0x57, 0x8A),
                      "Asystent głosowy", "HA Assist / Wyoming - planowane", SETTINGS_ACTION_NONE);

    add_settings_section(list, "SYSTEM", &y);
    add_settings_card(list, &y, ICON_DOWNLOAD, lv_color_make(0x38, 0x6A, 0x20),
                      "Aktualizacje", CURRENT_VERSION, SETTINGS_ACTION_UPDATES);
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
    if (auto_brightness_enabled) set_auto_brightness_enabled(false);
    manual_brightness_percent = percent;
    int raw_val = percent * backlight_max / 100;
    hal_set_backlight(raw_val);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED || lv_event_get_code(e) == LV_EVENT_PRESS_LOST) {
        save_int_file("/tuya/data/ha_brightness", manual_brightness_percent);
    }
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
    lv_obj_add_event_cb(control_center, control_center_drag_event_cb, LV_EVENT_ALL, NULL);

    backlight_max = read_int_file("/sys/class/backlight/backlight/max_brightness", 255);
    if (backlight_max < 1) backlight_max = 255;
    int brightness = auto_brightness_enabled ? auto_brightness_percent : manual_brightness_percent;

    hal_set_backlight(brightness * backlight_max / 100);
    brightness_slider = create_control_slider(control_center, ICON_BRIGHTNESS, 88, 5, brightness,
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
    lv_obj_set_size(handle_zone, 480, 80);
    lv_obj_align(handle_zone, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(handle_zone, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(handle_zone, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(handle_zone, 0, LV_PART_MAIN);
    lv_obj_clear_flag(handle_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(handle_zone, control_center_drag_event_cb, LV_EVENT_ALL, NULL);

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
    lv_obj_add_flag(btn1, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn1, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN); // Initial OFF (Red)
    lv_obj_set_style_bg_color(btn1, lv_color_make(0x00, 0xAA, 0x00), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(btn1, 37, LV_PART_MAIN);

    lv_obj_t * label1 = lv_label_create(btn1);
    lv_label_set_text(label1, ha_entity_1_name.c_str());
    lv_obj_set_style_text_color(label1, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(label1, LV_ALIGN_CENTER, 0, 0);

    // 5. Create minor Button: "Wentylator"
    lv_obj_t * btn2 = lv_btn_create(scr);
    lv_obj_set_size(btn2, 220, 55);
    lv_obj_align(btn2, LV_ALIGN_BOTTOM_MID, 0, -45);
    lv_obj_add_flag(btn2, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn2, lv_color_make(0xAA, 0x00, 0x00), LV_PART_MAIN);
    lv_obj_set_style_bg_color(btn2, lv_color_make(0x00, 0xAA, 0x00), LV_PART_MAIN | LV_STATE_CHECKED);
    lv_obj_set_style_radius(btn2, 27, LV_PART_MAIN);

    lv_obj_t * label2 = lv_label_create(btn2);
    lv_label_set_text(label2, ha_entity_2_name.c_str());
    lv_obj_set_style_text_color(label2, lv_color_make(255, 255, 255), LV_PART_MAIN);
    lv_obj_align(label2, LV_ALIGN_CENTER, 0, 0);

    ha_controls[0].entity_id = ha_entity_1;
    ha_controls[0].name = ha_entity_1_name;
    ha_controls[0].button = btn1;
    ha_controls[0].label = label1;
    ha_controls[1].entity_id = ha_entity_2;
    ha_controls[1].name = ha_entity_2_name;
    ha_controls[1].button = btn2;
    ha_controls[1].label = label2;
    lv_obj_add_event_cb(btn1, btn_event_cb, LV_EVENT_CLICKED, &ha_controls[0]);
    lv_obj_add_event_cb(btn2, btn_event_cb, LV_EVENT_CLICKED, &ha_controls[1]);
    update_control_state(&ha_controls[0]);
    update_control_state(&ha_controls[1]);
    lv_timer_create(state_poll_timer_cb, 5000, NULL);

    start_websocket_thread();

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
    std::string url = "http://" + ip + "/";
    
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

static void mqtt_telemetry_timer_cb(lv_timer_t * timer) {
    (void)timer;
    if (g_mqtt_client.is_connected()) {
        g_mqtt_client.publish_state("screen", g_screen_blanked ? "OFF" : "ON");
        g_mqtt_client.publish_state("brightness", std::to_string(g_active_backlight_raw));
        g_mqtt_client.publish_state("ip", get_wlan0_ip());
        g_mqtt_client.publish_state("ha_connected", ws_connected ? "true" : "false");
    }
}

int main(int argc, char **argv) {
    if (argc == 2 && (std::string(argv[1]) == "--cgi-config" || std::string(argv[1]) == "--cgi-status")) {
        g_cgi_mode = true;
        return std::string(argv[1]) == "--cgi-config" ? cgi_config_main() : cgi_status_main();
    }

    setbuf(stdout, NULL);
    printf("[HA Panel] Initializing Native LVGL Application...\n");
    
    // Clean old backup binary on start if it exists to release disk space
    unlink("/tuya/data/ha_panel.old");

    // Ensure DHCP daemon is active on wlan0
    if (system("pidof udhcpc >/dev/null") != 0) {
        system("udhcpc -i wlan0 -b -p /var/run/udhcpc.wlan0.pid >/dev/null 2>&1 &");
        printf("[Network] Started background udhcpc DHCP client on wlan0.\n");
    }
    
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

    backlight_max = read_int_file("/sys/class/backlight/backlight/max_brightness", 255);
    if (backlight_max < 1) backlight_max = 255;
    manual_brightness_percent = read_int_file("/tuya/data/ha_brightness", 80);
    if (manual_brightness_percent < 5 || manual_brightness_percent > 100) {
        manual_brightness_percent = 80;
    }
    auto_brightness_percent = manual_brightness_percent;
    auto_brightness_enabled = read_int_file("/tuya/data/ha_auto_brightness", 0) != 0;
    printf("[AutoBrightness] Mode: %s, manual level: %d%%.\n",
           auto_brightness_enabled ? "automatic" : "manual", manual_brightness_percent);

    // 3. Check for existing Home Assistant credentials
    if (!config_exists()) {
        printf("[Onboarding] No configuration file found. Starting Smart Onboarding...\n");
        onboarding_active = true;
        
        // Get panel's current IP address
        std::string ip = get_wlan0_ip();
        printf("[Onboarding] Panel IP: %s\n", ip.c_str());
        
        // Prepare CGI script permissions and launch HTTP server on port 80
        system("chmod +x /tuya/data/www/cgi-bin/save_config.sh 2>/dev/null");
        if (system("pidof httpd >/dev/null") != 0) {
            system("httpd -h /tuya/data/www -p 80 &");
        }
        
        // Show QR code onboarding UI
        create_onboarding_ui(ip);
        
        // Register 1-second interval timer to check for config
        lv_timer_create(config_poll_timer, 1000, NULL);
    } else {
        printf("[Onboarding] Configuration file exists. Loading active dashboard...\n");
        if (load_configuration()) {
            if (g_web_autostart) {
                system("iptables -I INPUT -p tcp --dport 80 -j ACCEPT 2>/dev/null");
                system("chmod +x /tuya/data/www/cgi-bin/* 2>/dev/null");
                if (system("pidof httpd >/dev/null") != 0) {
                    system("httpd -h /tuya/data/www -p 80 &");
                    printf("[WebPortal] Auto-started HTTP server on port 80.\n");
                } else {
                    printf("[WebPortal] HTTP server already active on port 80.\n");
                }
            }
            create_home_assistant_ui();
        } else {
            // Bad config file formatting, force onboarding
            unlink("/tuya/data/ha_config.json");
            printf("[Onboarding] Error reading config file. Redirecting to Onboarding...\n");
            char *args[] = {(char *)"/tuya/data/ha_panel", NULL};
            execv(args[0], args);
        }
    }

    // Load screen timeout setting (default: 30 seconds)
    int saved_timeout = read_int_file("/tuya/data/ha_screen_timeout", 30);
    screen_timeout_ms = saved_timeout * 1000;
    lv_disp_trig_activity(NULL);
    lv_timer_create(screensaver_timer_cb, 500, NULL);
    lv_timer_create(auto_brightness_timer_cb, 500, NULL);
    printf("[ScreenSaver] Screen blanker initialized. Timeout: %u seconds.\n", saved_timeout);

    // Initialize MQTT Client if configured
    g_mqtt_client.configure(g_mqtt_config);
    g_mqtt_client.set_command_callback([](const std::string& cmd, const std::string& payload) {
        printf("[MQTT Command] Received cmd='%s', payload='%s'\n", cmd.c_str(), payload.c_str());
        if (cmd == "screen") {
            if (payload == "ON") {
                hal_wake_screen();
            } else if (payload == "OFF") {
                hal_blank_screen();
            } else if (payload == "TOGGLE") {
                if (g_screen_blanked) hal_wake_screen();
                else hal_blank_screen();
            }
        } else if (cmd == "brightness") {
            int percent = atoi(payload.c_str());
            if (percent >= 0 && percent <= 100) {
                int raw_val = percent * backlight_max / 100;
                hal_set_backlight(raw_val);
            } else {
                int raw_val = atoi(payload.c_str());
                if (raw_val >= 0 && raw_val <= 255) hal_set_backlight(raw_val);
            }
        } else if (cmd == "reboot") {
            if (payload == "PRESS" || payload == "reboot") {
                printf("[MQTT Command] Reboot requested via MQTT!\n");
                system("reboot &");
            }
        } else if (cmd == "buzzer") {
            system("echo 1 > /sys/class/pwm/pwmchip0/pwm0/enable 2>/dev/null; usleep 50000; echo 0 > /sys/class/pwm/pwmchip0/pwm0/enable 2>/dev/null &");
        }
    });

    if (g_mqtt_config.enabled) {
        g_mqtt_client.start();
    }

    lv_timer_create(mqtt_telemetry_timer_cb, 5000, NULL);

    printf("[HA Panel] Main loop running. Rendering UI...\n");

    // 4. Main Event Processing loop
    while (1) {
        lv_timer_handler();
        usleep(5000); // 5ms sleep
    }

    hal_shutdown();
    return 0;
}
