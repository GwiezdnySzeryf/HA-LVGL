#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string>

#include "../lvgl/lvgl.h"

bool hal_display_init(void);
bool hal_touch_init(void);
void hal_shutdown(void);

LV_FONT_DECLARE(lv_font_control_icons_24);
LV_FONT_DECLARE(lv_font_montserrat_12);
LV_FONT_DECLARE(lv_font_montserrat_14);
LV_FONT_DECLARE(lv_font_montserrat_16);
LV_FONT_DECLARE(lv_font_montserrat_24);
LV_FONT_DECLARE(lv_font_montserrat_12_pl);
LV_FONT_DECLARE(lv_font_montserrat_14_pl);
LV_FONT_DECLARE(lv_font_montserrat_16_pl);
LV_FONT_DECLARE(lv_font_montserrat_20_pl);
LV_FONT_DECLARE(lv_font_montserrat_24_pl);

#define ICON_BACK       "\xEF\x81\xA0"
#define ICON_WIFI       "\xEF\x87\xAB"
#define ICON_LOCK       "\xEF\x80\xA3"
#define ICON_CHECK      "\xEF\x80\x8C"
#define ICON_INFO       "\xEF\x84\xA9"
#define ICON_SETTINGS   "\xEF\x80\x93"
#define ICON_CLOSE      "\xEF\x80\x8D"

static const lv_color_t SURFACE = lv_color_hex(0x111318);
static const lv_color_t SURFACE_CONTAINER = lv_color_hex(0x1D2024);
static const lv_color_t SURFACE_HIGH = lv_color_hex(0x282A2F);
static const lv_color_t OUTLINE = lv_color_hex(0x8D9199);
static const lv_color_t OUTLINE_VARIANT = lv_color_hex(0x43474E);
static const lv_color_t ON_SURFACE = lv_color_hex(0xE2E2E8);
static const lv_color_t ON_SURFACE_VARIANT = lv_color_hex(0xC3C6CF);
static const lv_color_t PRIMARY = lv_color_hex(0x4FD8E6);
static const lv_color_t ON_PRIMARY = lv_color_hex(0x00363D);
static const lv_color_t PRIMARY_CONTAINER = lv_color_hex(0x004F58);
static const lv_color_t ON_PRIMARY_CONTAINER = lv_color_hex(0x9CF0FA);
static const lv_color_t SUCCESS = lv_color_hex(0x91D18B);
static const lv_color_t ERROR_RED = lv_color_hex(0xFFB4AB);
static const lv_color_t ERROR_CONTAINER = lv_color_hex(0x93000A);

static lv_obj_t *content;
static lv_obj_t *keyboard = NULL;
static lv_obj_t *status_dot = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *wifi_switch = NULL;
static lv_obj_t *info_modal = NULL;
static lv_obj_t *ip_modal = NULL;
static lv_obj_t *connect_modal = NULL;

static bool is_connected = true;
static bool wifi_enabled = true;
static bool static_ip_mode = false;
static std::string active_ssid = "Dom_WiFi_5G";
static std::string selected_target_ssid = "";

static void surface(lv_obj_t *obj, lv_color_t color, int radius) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, int x, int y,
                       lv_color_t color, const lv_font_t *font) {
    lv_obj_t *obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    const lv_font_t * target_font = font;
    if (font == &lv_font_montserrat_12) target_font = &lv_font_montserrat_12_pl;
    else if (font == &lv_font_montserrat_14) target_font = &lv_font_montserrat_14_pl;
    else if (font == &lv_font_montserrat_16) target_font = &lv_font_montserrat_16_pl;
    else if (font == &lv_font_montserrat_24) target_font = &lv_font_montserrat_24_pl;
    lv_obj_set_style_text_font(obj, target_font, LV_PART_MAIN);
    return obj;
}

static void style_switch(lv_obj_t *sw) {
    lv_obj_set_size(sw, 48, 28);
    lv_obj_set_style_bg_color(sw, SURFACE_HIGH, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, OUTLINE, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, OUTLINE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, ON_PRIMARY, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN | LV_STATE_CHECKED);
}

static lv_obj_t *card(lv_obj_t *parent, int x, int y, int width, int height) {
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_pos(obj, x, y);
    surface(obj, SURFACE_CONTAINER, 24);
    return obj;
}

static lv_obj_t *icon_badge(lv_obj_t *parent, const char *symbol, int x, int y,
                            lv_color_t background, lv_color_t foreground) {
    lv_obj_t *badge = lv_obj_create(parent);
    lv_obj_set_size(badge, 44, 44);
    lv_obj_set_pos(badge, x, y);
    surface(badge, background, 22);
    lv_obj_t *glyph = label(badge, symbol, 0, 0, foreground, &lv_font_control_icons_24);
    lv_obj_center(glyph);
    return badge;
}

static void keyboard_event(lv_event_t *event) {
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *textarea = lv_event_get_target(event);
    if (code == LV_EVENT_FOCUSED) {
        if (!keyboard) {
            keyboard = lv_keyboard_create(lv_layer_top());
            lv_obj_set_size(keyboard, 480, 220);
            lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
            lv_obj_set_style_text_font(keyboard, &lv_font_montserrat_16_pl, LV_PART_ITEMS);
        }
        lv_keyboard_set_textarea(keyboard, textarea);
        lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED || code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *text_field(lv_obj_t *parent, const char *caption, const char *value,
                            const char *placeholder, int x, int y, int width, bool password) {
    label(parent, caption, x + 4, y, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_t *textarea = lv_textarea_create(parent);
    lv_obj_set_size(textarea, width, 44);
    lv_obj_set_pos(textarea, x, y + 20);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_placeholder_text(textarea, placeholder);
    lv_textarea_set_text(textarea, value);
    lv_textarea_set_password_mode(textarea, password);
    lv_obj_set_style_text_font(textarea, &lv_font_montserrat_16_pl, LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, ON_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(textarea, SURFACE_HIGH, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, PRIMARY, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_border_width(textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_width(textarea, 2, LV_PART_MAIN | LV_STATE_FOCUSED);
    lv_obj_set_style_radius(textarea, 14, LV_PART_MAIN);
    lv_obj_add_event_cb(textarea, keyboard_event, LV_EVENT_ALL, NULL);
    return textarea;
}

static void render_network_list(void);

static void update_wifi_status(void) {
    if (!wifi_enabled) {
        lv_label_set_text(status_label, "Wi-Fi wyłączone");
        surface(status_dot, OUTLINE, 4);
    } else if (is_connected) {
        lv_label_set_text(status_label, "Połączono");
        surface(status_dot, SUCCESS, 4);
    } else {
        lv_label_set_text(status_label, "Rozłączono");
        surface(status_dot, OUTLINE, 4);
    }
}

static void wifi_switch_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED) return;
    wifi_enabled = lv_obj_has_state(wifi_switch, LV_STATE_CHECKED);
    update_wifi_status();
    render_network_list();
}

static void disconnect_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    is_connected = false;
    update_wifi_status();
    render_network_list();
}

static void close_modal_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(event);
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    if (modal) lv_obj_del_async(modal);
}

static void execute_connect_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_obj_t *modal = (lv_obj_t *)lv_event_get_user_data(event);
    is_connected = true;
    if (!selected_target_ssid.empty()) active_ssid = selected_target_ssid;
    update_wifi_status();
    render_network_list();
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    if (modal) lv_obj_del_async(modal);
}

static void open_connect_modal_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    const char *ssid = (const char *)lv_event_get_user_data(event);
    if (ssid) selected_target_ssid = ssid;

    connect_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(connect_modal, 480, 480);
    lv_obj_set_pos(connect_modal, 0, 0);
    surface(connect_modal, SURFACE, 0);

    // Modal Header
    lv_obj_t *back = lv_btn_create(connect_modal);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    surface(back, SURFACE_HIGH, 24);
    lv_obj_add_event_cb(back, close_modal_cb, LV_EVENT_CLICKED, connect_modal);
    lv_obj_t *back_icon = label(back, ICON_BACK, 0, 0, ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    label(connect_modal, "Połącz z siecią", 76, 22, ON_SURFACE, &lv_font_montserrat_24_pl);

    // Content
    lv_obj_t *list = lv_obj_create(connect_modal);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    surface(list, SURFACE, 0);

    lv_obj_t *ssid_card = card(list, 20, 8, 440, 72);
    label(ssid_card, "Wybrana sieć", 16, 12, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    label(ssid_card, selected_target_ssid.c_str(), 16, 38, ON_SURFACE, &lv_font_montserrat_20_pl);

    text_field(list, "Hasło Wi-Fi", "", "Wpisz hasło sieci...", 20, 96, 440, true);

    lv_obj_t *cancel_btn = lv_btn_create(list);
    lv_obj_set_size(cancel_btn, 180, 48);
    lv_obj_set_pos(cancel_btn, 20, 184);
    surface(cancel_btn, SURFACE_HIGH, 24);
    lv_obj_add_event_cb(cancel_btn, close_modal_cb, LV_EVENT_CLICKED, connect_modal);
    lv_obj_t *cancel_lbl = label(cancel_btn, "Anuluj", 0, 0, ON_SURFACE_VARIANT, &lv_font_montserrat_16_pl);
    lv_obj_center(cancel_lbl);

    lv_obj_t *conn_btn = lv_btn_create(list);
    lv_obj_set_size(conn_btn, 240, 48);
    lv_obj_set_pos(conn_btn, 220, 184);
    surface(conn_btn, PRIMARY, 24);
    lv_obj_add_event_cb(conn_btn, execute_connect_cb, LV_EVENT_CLICKED, connect_modal);
    lv_obj_t *conn_lbl = label(conn_btn, "Połącz z siecią", 0, 0, ON_PRIMARY, &lv_font_montserrat_16_pl);
    lv_obj_center(conn_lbl);
}

static void open_info_modal_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    info_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(info_modal, 480, 480);
    lv_obj_set_pos(info_modal, 0, 0);
    surface(info_modal, SURFACE, 0);

    lv_obj_t *back = lv_btn_create(info_modal);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    surface(back, SURFACE_HIGH, 24);
    lv_obj_add_event_cb(back, close_modal_cb, LV_EVENT_CLICKED, info_modal);
    lv_obj_t *back_icon = label(back, ICON_BACK, 0, 0, ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    label(info_modal, "Szczegóły połączenia", 76, 22, ON_SURFACE, &lv_font_montserrat_24_pl);

    lv_obj_t *list = lv_obj_create(info_modal);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    surface(list, SURFACE, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    const char *keys[] = {
        "SSID / SIEĆ", "SIŁA SYGNAŁU", "ZABEZPIECZENIA", "ADRES MAC",
        "ADRES IPV4", "MASKA PODSIECI", "BRAMA (GATEWAY)", "DNS 1 / DNS 2", "INTERFEJS"
    };
    const char *vals[] = {
        active_ssid.c_str(), "-58 dBm (88%)", "WPA2-PSK (AES)", "8c:88:2b:00:07:14",
        "192.168.1.140", "255.255.255.0", "192.168.1.1", "8.8.8.8  •  4.2.2.2", "wlan0"
    };

    for (int i = 0; i < 9; ++i) {
        int y = 8 + i * 72;
        lv_obj_t *item = card(list, 20, y, 440, 64);
        label(item, keys[i], 16, 10, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
        label(item, vals[i], 16, 34, ON_SURFACE, &lv_font_montserrat_16_pl);
    }

    lv_obj_t *spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 656);
    surface(spacer, SURFACE, 0);
}

static void open_ip_modal_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;

    ip_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(ip_modal, 480, 480);
    lv_obj_set_pos(ip_modal, 0, 0);
    surface(ip_modal, SURFACE, 0);

    lv_obj_t *back = lv_btn_create(ip_modal);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    surface(back, SURFACE_HIGH, 24);
    lv_obj_add_event_cb(back, close_modal_cb, LV_EVENT_CLICKED, ip_modal);
    lv_obj_t *back_icon = label(back, ICON_BACK, 0, 0, ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    label(ip_modal, "Ustawienia IP", 76, 22, ON_SURFACE, &lv_font_montserrat_24_pl);

    lv_obj_t *list = lv_obj_create(ip_modal);
    lv_obj_set_size(list, 480, 410);
    lv_obj_set_pos(list, 0, 70);
    surface(list, SURFACE, 0);
    lv_obj_add_flag(list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *mode_card = card(list, 20, 8, 440, 72);
    label(mode_card, "Tryb konfiguracyjny", 16, 12, ON_SURFACE, &lv_font_montserrat_16_pl);
    label(mode_card, static_ip_mode ? "Statyczny adres IP" : "Automatyczny (DHCP)", 16, 40, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);

    lv_obj_t *btn_dhcp = lv_btn_create(mode_card);
    lv_obj_set_size(btn_dhcp, 140, 40);
    lv_obj_set_pos(btn_dhcp, 284, 16);
    surface(btn_dhcp, static_ip_mode ? SURFACE_HIGH : PRIMARY_CONTAINER, 20);
    lv_obj_t *lbl_dhcp = label(btn_dhcp, static_ip_mode ? "Użyj DHCP" : "DHCP", 0, 0, static_ip_mode ? ON_SURFACE_VARIANT : ON_PRIMARY_CONTAINER, &lv_font_montserrat_14_pl);
    lv_obj_center(lbl_dhcp);

    text_field(list, "Adres IPv4", "192.168.1.140", "192.168.1.x", 20, 92, 440, false);
    text_field(list, "Maska podsieci", "255.255.255.0", "255.255.255.0", 20, 168, 440, false);
    text_field(list, "Brama domyślna", "192.168.1.1", "192.168.1.1", 20, 244, 440, false);
    text_field(list, "Główny DNS", "8.8.8.8", "8.8.8.8", 20, 320, 212, false);
    text_field(list, "Zapasowy DNS", "4.2.2.2", "4.2.2.2", 248, 320, 212, false);

    lv_obj_t *save_btn = lv_btn_create(list);
    lv_obj_set_size(save_btn, 216, 48);
    lv_obj_set_pos(save_btn, 244, 400);
    surface(save_btn, PRIMARY, 24);
    lv_obj_add_event_cb(save_btn, close_modal_cb, LV_EVENT_CLICKED, ip_modal);
    lv_obj_t *save_lbl = label(save_btn, "Zapisz ustawienia", 0, 0, ON_PRIMARY, &lv_font_montserrat_16_pl);
    lv_obj_center(save_lbl);

    lv_obj_t *spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 458);
    surface(spacer, SURFACE, 0);
}

static lv_obj_t *network_list_container = NULL;

static void render_network_list(void) {
    if (!network_list_container) return;
    lv_obj_clean(network_list_container);

    if (!wifi_enabled) {
        lv_obj_t *disabled_card = card(network_list_container, 20, 0, 440, 80);
        label(disabled_card, "Karta Wi-Fi jest wyłączona", 16, 18, ON_SURFACE_VARIANT, &lv_font_montserrat_16_pl);
        label(disabled_card, "Włącz przełącznik powyżej, aby wyszukać sieci", 16, 46, OUTLINE, &lv_font_montserrat_14_pl);
        return;
    }

    int y = 0;

    // Section Header
    lv_obj_t *sec_hdr = label(network_list_container, "WYSZUKANE SIECI (3)", 24, y, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_set_style_text_letter_space(sec_hdr, 1, LV_PART_MAIN);
    y += 24;

    // Item 1: Active Connected Network
    lv_obj_t *item1 = card(network_list_container, 20, y, 440, 68);
    if (is_connected) {
        lv_obj_set_style_bg_color(item1, PRIMARY_CONTAINER, LV_PART_MAIN);
    }

    lv_obj_t *ssid1 = label(item1, active_ssid.c_str(), 16, 12, is_connected ? ON_PRIMARY_CONTAINER : ON_SURFACE, &lv_font_montserrat_16_pl);
    lv_obj_set_width(ssid1, 260);

    label(item1, is_connected ? "Sygnał: Bardzo dobry (88%) • WPA2/WPA3" : "Rozłączono z tą siecią", 16, 38, is_connected ? lv_color_hex(0x8BD7DF) : ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);

    lv_obj_t *btn1 = lv_btn_create(item1);
    lv_obj_set_size(btn1, 92, 36);
    lv_obj_set_pos(btn1, 332, 16);
    if (is_connected) {
        surface(btn1, ERROR_CONTAINER, 18);
        lv_obj_t *lbl1 = label(btn1, "Rozłącz", 0, 0, ERROR_RED, &lv_font_montserrat_14_pl);
        lv_obj_center(lbl1);
        lv_obj_add_event_cb(btn1, disconnect_cb, LV_EVENT_CLICKED, NULL);
    } else {
        surface(btn1, PRIMARY, 18);
        lv_obj_t *lbl1 = label(btn1, "Połącz", 0, 0, ON_PRIMARY, &lv_font_montserrat_14_pl);
        lv_obj_center(lbl1);
        lv_obj_add_event_cb(btn1, open_connect_modal_cb, LV_EVENT_CLICKED, (void *)"Dom_WiFi_5G");
    }

    y += 76;

    // Item 2: Router_Piętro_2G
    lv_obj_t *item2 = card(network_list_container, 20, y, 440, 68);
    label(item2, "Router_Piętro_2G", 16, 12, ON_SURFACE, &lv_font_montserrat_16_pl);
    label(item2, "Sygnał: Dobry (65%) • WPA2/WPA3", 16, 38, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);

    lv_obj_t *btn2 = lv_btn_create(item2);
    lv_obj_set_size(btn2, 92, 36);
    lv_obj_set_pos(btn2, 332, 16);
    surface(btn2, PRIMARY, 18);
    lv_obj_t *lbl2 = label(btn2, "Połącz", 0, 0, ON_PRIMARY, &lv_font_montserrat_14_pl);
    lv_obj_center(lbl2);
    lv_obj_add_event_cb(btn2, open_connect_modal_cb, LV_EVENT_CLICKED, (void *)"Router_Piętro_2G");

    y += 76;

    // Item 3: Sąsiedzi_Guest
    lv_obj_t *item3 = card(network_list_container, 20, y, 440, 68);
    label(item3, "Sąsiedzi_Guest", 16, 12, ON_SURFACE, &lv_font_montserrat_16_pl);
    label(item3, "Sygnał: Słaby (32%) • Sieć otwarta", 16, 38, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);

    lv_obj_t *btn3 = lv_btn_create(item3);
    lv_obj_set_size(btn3, 92, 36);
    lv_obj_set_pos(btn3, 332, 16);
    surface(btn3, PRIMARY, 18);
    lv_obj_t *lbl3 = label(btn3, "Połącz", 0, 0, ON_PRIMARY, &lv_font_montserrat_14_pl);
    lv_obj_center(lbl3);
    lv_obj_add_event_cb(btn3, open_connect_modal_cb, LV_EVENT_CLICKED, (void *)"Sąsiedzi_Guest");

    y += 84;

    // Manual Connection Header & Fields
    lv_obj_t *manual_hdr = label(network_list_container, "POŁĄCZ Z INNĄ SIECIĄ", 24, y, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_set_style_text_letter_space(manual_hdr, 1, LV_PART_MAIN);
    y += 24;

    text_field(network_list_container, "Nazwa sieci (SSID)", "", "Wpisz nazwę...", 20, y, 440, false);
    y += 74;
    text_field(network_list_container, "Hasło WPA/WPA2", "", "Wpisz hasło...", 20, y, 440, true);
    y += 78;

    lv_obj_t *connect_btn = lv_btn_create(network_list_container);
    lv_obj_set_size(connect_btn, 216, 48);
    lv_obj_set_pos(connect_btn, 244, y);
    surface(connect_btn, PRIMARY, 24);
    lv_obj_set_style_bg_color(connect_btn, lv_color_hex(0x3DC3D1), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_t *btn_lbl = label(connect_btn, "Połącz z siecią", 0, 0, ON_PRIMARY, &lv_font_montserrat_16_pl);
    lv_obj_center(btn_lbl);

    y += 60;

    lv_obj_t *spacer = lv_obj_create(network_list_container);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, y);
    surface(spacer, SURFACE, 0);
}

static void back_clicked(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) exit(0);
}

static void create_wifi_preview(void) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    // Header Bar: "Wi-Fi"
    lv_obj_t *back = lv_btn_create(screen);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 8, 8);
    surface(back, SURFACE, 24);
    lv_obj_set_style_bg_color(back, SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_icon = label(back, ICON_BACK, 0, 0, ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    label(screen, "Wi-Fi", 68, 20, ON_SURFACE, &lv_font_montserrat_24_pl);

    // Scrollable Content
    content = lv_obj_create(screen);
    lv_obj_set_size(content, 480, 410);
    lv_obj_set_pos(content, 0, 70);
    surface(content, SURFACE, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);

    // 1. Hero Card: Status ONLY (Połączono / Rozłączono / Wyłączone)
    lv_obj_t *hero = card(content, 20, 8, 440, 76);
    lv_obj_set_style_bg_color(hero, PRIMARY_CONTAINER, LV_PART_MAIN);

    status_dot = lv_obj_create(hero);
    lv_obj_set_size(status_dot, 8, 8);
    lv_obj_set_pos(status_dot, 16, 34);
    surface(status_dot, SUCCESS, 4);

    status_label = label(hero, "Połączono", 34, 24, ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    icon_badge(hero, ICON_WIFI, 380, 16, lv_color_hex(0x116872), ON_PRIMARY_CONTAINER);

    // 2. Wi-Fi Switch Card: ONLY "Wi-Fi" label
    lv_obj_t *toggle_card = card(content, 20, 96, 440, 68);
    label(toggle_card, "Wi-Fi", 16, 22, ON_SURFACE, &lv_font_montserrat_20_pl);

    wifi_switch = lv_switch_create(toggle_card);
    style_switch(wifi_switch);
    lv_obj_set_pos(wifi_switch, 374, 20);
    lv_obj_add_state(wifi_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(wifi_switch, wifi_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // 3. Side-By-Side Action Cards: [Więcej informacji] [Ustawienia IP]
    lv_obj_t *info_btn = card(content, 20, 176, 212, 72);
    lv_obj_add_flag(info_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(info_btn, SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    label(info_btn, "Więcej informacji", 14, 14, ON_SURFACE, &lv_font_montserrat_16_pl);
    label(info_btn, "Sygnał, MAC, IP", 14, 42, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_add_event_cb(info_btn, open_info_modal_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ip_btn = card(content, 248, 176, 212, 72);
    lv_obj_add_flag(ip_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(ip_btn, SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    label(ip_btn, "Ustawienia IP", 14, 14, ON_SURFACE, &lv_font_montserrat_16_pl);
    label(ip_btn, "DHCP / Statyczny IP", 14, 42, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_add_event_cb(ip_btn, open_ip_modal_cb, LV_EVENT_CLICKED, NULL);

    // 4. Dynamic Network List Container
    network_list_container = lv_obj_create(content);
    lv_obj_set_size(network_list_container, 480, 500);
    lv_obj_set_pos(network_list_container, 0, 260);
    surface(network_list_container, SURFACE, 0);
    lv_obj_clear_flag(network_list_container, LV_OBJ_FLAG_SCROLLABLE);

    update_wifi_status();
    render_network_list();
}

int main(void) {
    lv_init();
    if (!hal_display_init() || !hal_touch_init()) return 1;
    create_wifi_preview();
    while (true) {
        lv_timer_handler();
        usleep(5000);
    }
    hal_shutdown();
    return 0;
}
