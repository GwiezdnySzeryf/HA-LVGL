#include <stdlib.h>
#include <unistd.h>

#include "../lvgl/lvgl.h"
#include "../lvgl/src/extra/libs/qrcode/lv_qrcode.h"

bool hal_display_init(void);
bool hal_touch_init(void);
void hal_shutdown(void);

LV_FONT_DECLARE(lv_font_control_icons_24);
LV_FONT_DECLARE(lv_font_montserrat_16_pl);
LV_FONT_DECLARE(lv_font_montserrat_20_pl);

#define ICON_BACK  "\xEF\x81\xA0"
#define ICON_GLOBE "\xEF\x82\xAC"

static const lv_color_t COLOR_SURFACE = lv_color_hex(0x111318);
static const lv_color_t COLOR_SURFACE_CONTAINER = lv_color_hex(0x1D2024);
static const lv_color_t COLOR_SURFACE_HIGH = lv_color_hex(0x282A2F);
static const lv_color_t COLOR_OUTLINE = lv_color_hex(0x8D9199);
static const lv_color_t COLOR_OUTLINE_VARIANT = lv_color_hex(0x43474E);
static const lv_color_t COLOR_ON_SURFACE = lv_color_hex(0xE2E2E8);
static const lv_color_t COLOR_ON_SURFACE_VARIANT = lv_color_hex(0xC3C6CF);
static const lv_color_t COLOR_PRIMARY = lv_color_hex(0x4FD8E6);
static const lv_color_t COLOR_ON_PRIMARY = lv_color_hex(0x00363D);
static const lv_color_t COLOR_PRIMARY_CONTAINER = lv_color_hex(0x004F58);
static const lv_color_t COLOR_ON_PRIMARY_CONTAINER = lv_color_hex(0x9CF0FA);
static const lv_color_t COLOR_TERTIARY = lv_color_hex(0x91D18B);

static lv_obj_t *server_switch = NULL;
static lv_obj_t *status_dot = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *restart_button = NULL;
static lv_obj_t *restart_label = NULL;
static lv_obj_t *snackbar = NULL;

static void make_surface(lv_obj_t *obj, lv_color_t color, int radius) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y,
                            lv_color_t color, const lv_font_t *font) {
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

static void style_switch(lv_obj_t *sw) {
    lv_obj_set_size(sw, 48, 28);
    lv_obj_set_style_bg_color(sw, COLOR_SURFACE_HIGH, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, COLOR_OUTLINE, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, COLOR_OUTLINE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, COLOR_PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, COLOR_ON_PRIMARY, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN | LV_STATE_CHECKED);
}

static void update_server_state(void) {
    bool enabled = lv_obj_has_state(server_switch, LV_STATE_CHECKED);
    lv_label_set_text(status_label, enabled ? "Serwer włączony" : "Serwer wyłączony");
    lv_obj_set_style_bg_color(status_dot, enabled ? COLOR_TERTIARY : COLOR_OUTLINE, LV_PART_MAIN);
    if (enabled) {
        lv_obj_clear_state(restart_button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(restart_button, LV_STATE_DISABLED);
    }
}

static void server_switch_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED) update_server_state();
}

static void hide_snackbar_cb(lv_timer_t *timer) {
    lv_obj_add_flag(snackbar, LV_OBJ_FLAG_HIDDEN);
    lv_timer_del(timer);
}

static void finish_restart_cb(lv_timer_t *timer) {
    lv_label_set_text(restart_label, "Restart serwera");
    lv_obj_clear_state(restart_button, LV_STATE_DISABLED);
    lv_obj_clear_flag(snackbar, LV_OBJ_FLAG_HIDDEN);
    lv_timer_create(hide_snackbar_cb, 2100, NULL);
    lv_timer_del(timer);
}

static void restart_button_cb(lv_event_t *event) {
    if (lv_event_get_code(event) != LV_EVENT_CLICKED) return;
    lv_label_set_text(restart_label, "Restartowanie...");
    lv_obj_add_state(restart_button, LV_STATE_DISABLED);
    lv_timer_create(finish_restart_cb, 900, NULL);
}

static void back_button_cb(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) exit(0);
}

static void create_portal_preview(void) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, COLOR_SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(screen);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 8, 8);
    make_surface(back, COLOR_SURFACE, 24);
    lv_obj_set_style_bg_color(back, COLOR_SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, back_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_icon = make_label(back, ICON_BACK, 0, 0, COLOR_ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    make_label(screen, "Portal WWW", 68, 20, COLOR_ON_SURFACE, &lv_font_montserrat_24);

    lv_obj_t *status = lv_obj_create(screen);
    lv_obj_set_size(status, 440, 76);
    lv_obj_set_pos(status, 20, 64);
    make_surface(status, COLOR_PRIMARY_CONTAINER, 28);

    status_dot = lv_obj_create(status);
    lv_obj_set_size(status_dot, 8, 8);
    lv_obj_set_pos(status_dot, 16, 22);
    make_surface(status_dot, COLOR_TERTIARY, 4);

    status_label = make_label(status, "Serwer włączony", 34, 10, COLOR_ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    make_label(status, "192.168.1.140", 16, 43, lv_color_hex(0x8BD7DF), &lv_font_montserrat_14);

    lv_obj_t *globe_bg = lv_obj_create(status);
    lv_obj_set_size(globe_bg, 44, 44);
    lv_obj_set_pos(globe_bg, 380, 16);
    make_surface(globe_bg, lv_color_hex(0x116872), 22);
    lv_obj_t *globe = make_label(globe_bg, ICON_GLOBE, 0, 0, COLOR_ON_PRIMARY_CONTAINER, &lv_font_control_icons_24);
    lv_obj_center(globe);

    lv_obj_t *controls = lv_obj_create(screen);
    lv_obj_set_size(controls, 208, 306);
    lv_obj_set_pos(controls, 20, 152);
    make_surface(controls, COLOR_SURFACE_CONTAINER, 28);

    lv_obj_t *overline = make_label(controls, "STEROWANIE", 16, 17, COLOR_ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_set_style_text_letter_space(overline, 1, LV_PART_MAIN);

    lv_obj_t *server_label = make_label(controls, "Serwer\nWWW", 16, 49, COLOR_ON_SURFACE, &lv_font_montserrat_20_pl);
    lv_obj_set_width(server_label, 105);
    lv_obj_set_style_text_line_space(server_label, 1, LV_PART_MAIN);
    server_switch = lv_switch_create(controls);
    style_switch(server_switch);
    lv_obj_set_pos(server_switch, 144, 54);
    lv_obj_add_state(server_switch, LV_STATE_CHECKED);
    lv_obj_add_event_cb(server_switch, server_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *divider = lv_obj_create(controls);
    lv_obj_set_size(divider, 176, 1);
    lv_obj_set_pos(divider, 16, 119);
    make_surface(divider, COLOR_OUTLINE_VARIANT, 0);

    lv_obj_t *auto_label = make_label(controls, "Autostart\nserwera", 16, 132, COLOR_ON_SURFACE, &lv_font_montserrat_20_pl);
    lv_label_set_long_mode(auto_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(auto_label, 105);
    lv_obj_set_style_text_line_space(auto_label, 1, LV_PART_MAIN);

    lv_obj_t *auto_switch = lv_switch_create(controls);
    style_switch(auto_switch);
    lv_obj_set_pos(auto_switch, 144, 140);
    lv_obj_add_state(auto_switch, LV_STATE_CHECKED);

    restart_button = lv_btn_create(controls);
    lv_obj_set_size(restart_button, 176, 48);
    lv_obj_set_pos(restart_button, 16, 238);
    make_surface(restart_button, COLOR_SURFACE_HIGH, 24);
    lv_obj_set_style_bg_color(restart_button, lv_color_hex(0x343B40), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(restart_button, LV_OPA_40, LV_PART_MAIN | LV_STATE_DISABLED);
    lv_obj_add_event_cb(restart_button, restart_button_cb, LV_EVENT_CLICKED, NULL);

    restart_label = make_label(restart_button, "Restart serwera", 0, 0, COLOR_PRIMARY, &lv_font_montserrat_20_pl);
    lv_obj_center(restart_label);

    lv_obj_t *qr_card = lv_obj_create(screen);
    lv_obj_set_size(qr_card, 220, 306);
    lv_obj_set_pos(qr_card, 240, 152);
    make_surface(qr_card, COLOR_SURFACE_CONTAINER, 28);

    lv_obj_t *qr = lv_qrcode_create(qr_card, 150, lv_color_black(), lv_color_white());
    const char *url = "http://192.168.1.140/";
    lv_qrcode_update(qr, url, 22);
    lv_obj_set_pos(qr, 35, 24);

    lv_obj_t *qr_title = make_label(qr_card, "Zeskanuj kod QR i\nprzejdź do panelu", 0, 0, COLOR_ON_SURFACE, &lv_font_montserrat_20_pl);
    lv_obj_set_style_text_align(qr_title, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(qr_title, LV_ALIGN_TOP_MID, 0, 192);
    lv_obj_t *qr_url = make_label(qr_card, url, 0, 0, COLOR_ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_align(qr_url, LV_ALIGN_TOP_MID, 0, 258);

    snackbar = lv_obj_create(screen);
    lv_obj_set_size(snackbar, 440, 48);
    lv_obj_set_pos(snackbar, 20, 416);
    make_surface(snackbar, COLOR_ON_SURFACE, 8);
    make_label(snackbar, "Serwer został uruchomiony ponownie", 16, 15, COLOR_SURFACE, &lv_font_montserrat_16_pl);
    lv_obj_add_flag(snackbar, LV_OBJ_FLAG_HIDDEN);

    update_server_state();
}

int main(void) {
    lv_init();
    if (!hal_display_init() || !hal_touch_init()) return 1;
    create_portal_preview();

    while (true) {
        lv_timer_handler();
        usleep(5000);
    }

    hal_shutdown();
    return 0;
}
