#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

#include "../lvgl/lvgl.h"

bool hal_display_init(void);
bool hal_touch_init(void);
void hal_shutdown(void);

LV_FONT_DECLARE(lv_font_control_icons_24);
LV_FONT_DECLARE(lv_font_montserrat_14_pl);
LV_FONT_DECLARE(lv_font_montserrat_16_pl);
LV_FONT_DECLARE(lv_font_montserrat_20_pl);

#define ICON_BACK       "\xEF\x81\xA0"
#define ICON_BRIGHTNESS "\xEF\x86\x85"
#define ICON_DOWNLOAD   "\xEF\x80\x99"
#define ICON_INFO       "\xEF\x84\xA9"
#define ICON_PLUG       "\xEF\x87\xA6"

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

static lv_obj_t *content;
static lv_obj_t *nav_buttons[4];
static lv_obj_t *keyboard;
static lv_obj_t *brightness_value;
static lv_obj_t *timeout_buttons[4];

static void show_page(int page);

static bool keyboard_symbols_available(void) {
    const uint32_t symbols[] = {0xF00C, 0xF053, 0xF054, 0xF11C, 0xF55A, 0xF8A2};
    lv_font_glyph_dsc_t glyph;
    for (unsigned int i = 0; i < sizeof(symbols) / sizeof(symbols[0]); ++i) {
        if (!lv_font_get_glyph_dsc(&lv_font_montserrat_16_pl, &glyph, symbols[i], 0)) return false;
    }
    return true;
}

static bool polish_glyphs_available(const lv_font_t *font) {
    const uint32_t glyphs[] = {
        0x0104, 0x0105, 0x0106, 0x0107, 0x0118, 0x0119, 0x0141, 0x0142, 0x0143,
        0x0144, 0x00D3, 0x00F3, 0x015A, 0x015B, 0x0179, 0x017A, 0x017B, 0x017C
    };
    lv_font_glyph_dsc_t glyph;
    for (unsigned int i = 0; i < sizeof(glyphs) / sizeof(glyphs[0]); ++i) {
        if (!lv_font_get_glyph_dsc(font, &glyph, glyphs[i], 0)) return false;
    }
    return true;
}

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
    lv_obj_set_style_text_font(obj,
                               font == &lv_font_montserrat_14 ? &lv_font_montserrat_14_pl : font,
                               LV_PART_MAIN);
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

static lv_obj_t *filled_button(lv_obj_t *parent, const char *text, int x, int y,
                               int width, lv_color_t background, lv_color_t foreground) {
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, 48);
    lv_obj_set_pos(button, x, y);
    surface(button, background, 24);
    lv_obj_set_style_bg_color(button, lv_color_hex(0x116872), LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_t *text_label = label(button, text, 0, 0, foreground, &lv_font_montserrat_16_pl);
    lv_obj_center(text_label);
    return button;
}

static void make_hero(const char *title, const char *subtitle, const char *icon,
                      bool active) {
    lv_obj_t *hero = card(content, 20, 8, 440, 76);
    lv_obj_set_style_bg_color(hero, PRIMARY_CONTAINER, LV_PART_MAIN);
    lv_obj_t *dot = lv_obj_create(hero);
    lv_obj_set_size(dot, 8, 8);
    lv_obj_set_pos(dot, 16, 22);
    surface(dot, active ? SUCCESS : OUTLINE, 4);
    label(hero, title, 34, 10, ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    label(hero, subtitle, 16, 44, lv_color_hex(0x8BD7DF), &lv_font_montserrat_14);
    icon_badge(hero, icon, 380, 16, lv_color_hex(0x116872), ON_PRIMARY_CONTAINER);
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
                            int x, int y, int width) {
    label(parent, caption, x + 4, y, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t *textarea = lv_textarea_create(parent);
    lv_obj_set_size(textarea, width, 48);
    lv_obj_set_pos(textarea, x, y + 22);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_text(textarea, value);
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

static void create_mqtt_page(void) {
    make_hero("MQTT połączony", "192.168.1.73:1883", ICON_PLUG, true);

    lv_obj_t *client = card(content, 20, 96, 440, 76);
    label(client, "Klient MQTT", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    label(client, "Publikowanie danych panelu", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t *sw = lv_switch_create(client);
    style_switch(sw);
    lv_obj_set_pos(sw, 374, 24);
    lv_obj_add_state(sw, LV_STATE_CHECKED);

    text_field(content, "Broker", "192.168.1.73", 20, 188, 292);
    text_field(content, "Port", "1883", 324, 188, 136);
    text_field(content, "Temat bazowy", "panel/tpp01", 20, 266, 440);
    filled_button(content, "Zapisz i połącz", 244, 344, 216, PRIMARY, ON_PRIMARY);
}

static void create_updates_page(void) {
    make_hero("System aktualny", "Wersja 1.7.0", ICON_DOWNLOAD, true);

    lv_obj_t *release = card(content, 20, 96, 440, 116);
    label(release, "NAJNOWSZE WYDANIE", 16, 14, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    label(release, "HA Panel 1.7.0", 16, 42, ON_SURFACE, &lv_font_montserrat_20_pl);
    label(release, "Kanał stabilny  •  GitHub", 16, 76, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t *badge = lv_obj_create(release);
    lv_obj_set_size(badge, 78, 32);
    lv_obj_set_pos(badge, 346, 42);
    surface(badge, lv_color_hex(0x204E35), 16);
    lv_obj_t *badge_label = label(badge, "AKTUALNA", 0, 0, SUCCESS, &lv_font_montserrat_12);
    lv_obj_center(badge_label);

    lv_obj_t *network = card(content, 20, 224, 440, 72);
    label(network, "Połączenie z serwerem wydań", 16, 10, ON_SURFACE, &lv_font_montserrat_16_pl);
    label(network, "Gotowe do sprawdzenia", 16, 40, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t *dot = lv_obj_create(network);
    lv_obj_set_size(dot, 10, 10);
    lv_obj_set_pos(dot, 406, 31);
    surface(dot, SUCCESS, 5);

    filled_button(content, "Sprawdź aktualizacje", 20, 316, 440, PRIMARY, ON_PRIMARY);
}

static void spec_card(int x, int y, const char *caption, const char *value) {
    lv_obj_t *spec = card(content, x, y, 212, 92);
    label(spec, caption, 14, 14, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t *value_label = label(spec, value, 14, 43, ON_SURFACE, &lv_font_montserrat_16_pl);
    lv_obj_set_width(value_label, 184);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
}

static void create_info_page(void) {
    make_hero("TPP01-Z", "Panel Home Assistant", ICON_INFO, true);
    spec_card(20, 96, "OPROGRAMOWANIE", "Wersja 1.7.0");
    spec_card(248, 96, "SILNIK UI", "LVGL 8.3.11");
    spec_card(20, 200, "SIEĆ", "192.168.1.140");
    spec_card(248, 200, "PLATFORMA", "AArch64 Linux");

    lv_obj_t *project = card(content, 20, 304, 440, 62);
    label(project, "Projekt open source", 16, 9, ON_SURFACE, &lv_font_montserrat_16_pl);
    label(project, "GwiezdnySzeryf / HA-LVGL", 16, 35, PRIMARY, &lv_font_montserrat_14);
}

static void brightness_changed(lv_event_t *event) {
    char value[12];
    snprintf(value, sizeof(value), "%d%%", (int)lv_slider_get_value(lv_event_get_target(event)));
    lv_label_set_text(brightness_value, value);
}

static void timeout_clicked(lv_event_t *event) {
    lv_obj_t *selected = lv_event_get_target(event);
    for (int i = 0; i < 4; ++i) {
        bool active = timeout_buttons[i] == selected;
        lv_obj_set_style_bg_color(timeout_buttons[i], active ? PRIMARY_CONTAINER : SURFACE_HIGH, LV_PART_MAIN);
        lv_obj_set_style_border_width(timeout_buttons[i], active ? 0 : 1, LV_PART_MAIN);
        lv_obj_t *text = lv_obj_get_child(timeout_buttons[i], 0);
        lv_obj_set_style_text_color(text, active ? ON_PRIMARY_CONTAINER : ON_SURFACE_VARIANT, LV_PART_MAIN);
    }
}

static void create_display_page(void) {
    make_hero("Ekran aktywny", "Jasność 72%", ICON_BRIGHTNESS, true);

    lv_obj_t *brightness = card(content, 20, 96, 440, 126);
    label(brightness, "Jasność ekranu", 16, 14, ON_SURFACE, &lv_font_montserrat_20_pl);
    brightness_value = label(brightness, "72%", 0, 16, PRIMARY, &lv_font_montserrat_20_pl);
    lv_obj_align(brightness_value, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_t *slider = lv_slider_create(brightness);
    lv_obj_set_size(slider, 408, 20);
    lv_obj_set_pos(slider, 16, 77);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, 72, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, ON_PRIMARY_CONTAINER, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, brightness_changed, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *timeout = card(content, 20, 234, 440, 132);
    label(timeout, "Wygaszanie ekranu", 16, 14, ON_SURFACE, &lv_font_montserrat_20_pl);
    label(timeout, "Po czasie bezczynności", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    const char *values[] = {"15 s", "30 s", "1 min", "Nigdy"};
    for (int i = 0; i < 4; ++i) {
        timeout_buttons[i] = lv_btn_create(timeout);
        lv_obj_set_size(timeout_buttons[i], 94, 40);
        lv_obj_set_pos(timeout_buttons[i], 16 + i * 102, 78);
        surface(timeout_buttons[i], i == 1 ? PRIMARY_CONTAINER : SURFACE_HIGH, 20);
        lv_obj_set_style_border_color(timeout_buttons[i], OUTLINE_VARIANT, LV_PART_MAIN);
        lv_obj_set_style_border_width(timeout_buttons[i], i == 1 ? 0 : 1, LV_PART_MAIN);
        lv_obj_t *text = label(timeout_buttons[i], values[i], 0, 0,
                               i == 1 ? ON_PRIMARY_CONTAINER : ON_SURFACE_VARIANT,
                               &lv_font_montserrat_14);
        lv_obj_center(text);
        lv_obj_add_event_cb(timeout_buttons[i], timeout_clicked, LV_EVENT_CLICKED, NULL);
    }
}

static void show_page(int page) {
    if (keyboard) lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clean(content);
    for (int i = 0; i < 4; ++i) {
        bool selected = i == page;
        lv_obj_set_style_bg_color(nav_buttons[i], selected ? PRIMARY_CONTAINER : SURFACE, LV_PART_MAIN);
        lv_obj_t *text = lv_obj_get_child(nav_buttons[i], 0);
        lv_obj_set_style_text_color(text, selected ? ON_PRIMARY_CONTAINER : ON_SURFACE_VARIANT, LV_PART_MAIN);
    }
    if (page == 0) create_mqtt_page();
    else if (page == 1) create_updates_page();
    else if (page == 2) create_info_page();
    else create_display_page();
}

static void nav_clicked(lv_event_t *event) {
    int page = (int)(long)lv_event_get_user_data(event);
    show_page(page);
}

static void back_clicked(lv_event_t *event) {
    if (lv_event_get_code(event) == LV_EVENT_CLICKED) exit(0);
}

static void create_preview(void) {
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, SURFACE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *back = lv_btn_create(screen);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 8, 8);
    surface(back, SURFACE, 24);
    lv_obj_set_style_bg_color(back, SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
    lv_obj_add_event_cb(back, back_clicked, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_icon = label(back, ICON_BACK, 0, 0, ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);
    label(screen, "Ustawienia panelu", 68, 20, ON_SURFACE, &lv_font_montserrat_24);

    const char *names[] = {"MQTT", "Aktualiz.", "Info", "Ekran"};
    for (int i = 0; i < 4; ++i) {
        nav_buttons[i] = lv_btn_create(screen);
        lv_obj_set_size(nav_buttons[i], 106, 40);
        lv_obj_set_pos(nav_buttons[i], 20 + i * 110, 68);
        surface(nav_buttons[i], SURFACE, 20);
        lv_obj_set_style_bg_color(nav_buttons[i], SURFACE_HIGH, LV_PART_MAIN | LV_STATE_PRESSED);
        lv_obj_t *text = label(nav_buttons[i], names[i], 0, 0, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
        lv_obj_center(text);
        lv_obj_add_event_cb(nav_buttons[i], nav_clicked, LV_EVENT_CLICKED, (void *)(long)i);
    }

    content = lv_obj_create(screen);
    lv_obj_set_size(content, 480, 366);
    lv_obj_set_pos(content, 0, 114);
    surface(content, SURFACE, 0);
    lv_obj_add_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_OFF);
    show_page(0);
}

int main(int argc, char **argv) {
    lv_init();
    if (argc == 2 && strcmp(argv[1], "--check-keyboard-font") == 0) {
        bool keyboard_available = keyboard_symbols_available();
        bool polish_available = polish_glyphs_available(&lv_font_montserrat_14_pl) &&
                                polish_glyphs_available(&lv_font_montserrat_16_pl) &&
                                polish_glyphs_available(&lv_font_montserrat_20_pl);
        printf("Keyboard symbols: %s\n", keyboard_available ? "available" : "missing");
        printf("Polish glyphs (14/16/20 px): %s\n", polish_available ? "available" : "missing");
        return keyboard_available && polish_available ? 0 : 1;
    }
    if (!hal_display_init() || !hal_touch_init()) return 1;
    create_preview();
    while (true) {
        lv_timer_handler();
        usleep(5000);
    }
    hal_shutdown();
    return 0;
}
