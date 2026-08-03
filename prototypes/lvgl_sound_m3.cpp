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
#define ICON_VOLUME     "\xEF\x80\xA8"

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

static lv_obj_t * screen = NULL;
static lv_obj_t * list_container = NULL;
static int active_version = 0;
static int master_volume = 72;
static int profile_index = 0;
static bool muted = false;
static bool touch_sound = true;
static bool ha_notifications = true;
static bool quiet_hours = true;
static bool critical_alerts = true;

static lv_obj_t * ver_a_btn = NULL;
static lv_obj_t * ver_b_btn = NULL;
static lv_obj_t * ver_c_btn = NULL;
static lv_obj_t * volume_value_label = NULL;

static void create_sound_screen(int version);

static void make_surface(lv_obj_t * obj, lv_color_t color, int radius) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t * make_card(lv_obj_t * parent, int x, int y, int w, int h) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, w, h);
    lv_obj_set_pos(card, x, y);
    make_surface(card, SURFACE_CONTAINER, 24);
    return card;
}

static lv_obj_t * make_label(lv_obj_t * parent, const char * text, int x, int y, lv_color_t color, const lv_font_t * font) {
    lv_obj_t * label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
    return label;
}

static void style_switch(lv_obj_t * sw) {
    lv_obj_set_size(sw, 48, 28);
    lv_obj_set_style_bg_color(sw, SURFACE_HIGH, LV_PART_MAIN);
    lv_obj_set_style_border_color(sw, OUTLINE, LV_PART_MAIN);
    lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, OUTLINE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(sw, PRIMARY, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(sw, ON_PRIMARY, LV_PART_KNOB | LV_STATE_CHECKED);
    lv_obj_set_style_border_width(sw, 0, LV_PART_MAIN | LV_STATE_CHECKED);
}

static void toggle_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    bool * value = static_cast<bool *>(lv_event_get_user_data(e));
    *value = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static lv_obj_t * make_switch(lv_obj_t * parent, int y, bool * value) {
    lv_obj_t * sw = lv_switch_create(parent);
    style_switch(sw);
    lv_obj_set_pos(sw, 374, y);
    if (*value) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, toggle_event_cb, LV_EVENT_VALUE_CHANGED, value);
    return sw;
}

static void volume_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    master_volume = lv_slider_get_value(lv_event_get_target(e));
    char text[16];
    snprintf(text, sizeof(text), "%d%%", master_volume);
    lv_label_set_text(volume_value_label, text);
}

static lv_obj_t * make_volume_slider(lv_obj_t * parent, int y) {
    lv_obj_t * slider = lv_slider_create(parent);
    lv_obj_set_size(slider, 408, 20);
    lv_obj_set_pos(slider, 16, y);
    lv_slider_set_range(slider, 0, 100);
    lv_slider_set_value(slider, master_volume, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, ON_PRIMARY_CONTAINER, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, volume_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    return slider;
}

static void profile_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    profile_index = (int)(long)lv_event_get_user_data(e);
    static const int levels[] = {72, 35, 15, 0};
    master_volume = levels[profile_index];
    muted = profile_index == 3;
    create_sound_screen(active_version);
}

static void test_sound_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    lv_obj_t * label = static_cast<lv_obj_t *>(lv_event_get_user_data(e));
    lv_label_set_text(label, muted ? "Głośnik jest wyciszony" : "Sygnał testowy odtworzony");
}

static lv_obj_t * make_test_button(lv_obj_t * parent, int y) {
    lv_obj_t * button = lv_btn_create(parent);
    lv_obj_set_size(button, 408, 44);
    lv_obj_set_pos(button, 16, y);
    make_surface(button, PRIMARY_CONTAINER, 22);
    lv_obj_t * label = make_label(button, "Odtwórz dźwięk testowy", 0, 0, ON_PRIMARY_CONTAINER, &lv_font_montserrat_14_pl);
    lv_obj_center(label);
    lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, test_sound_event_cb, LV_EVENT_CLICKED, label);
    return button;
}

static void version_switch_event_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int target_ver = (int)(long)lv_event_get_user_data(e);
    if (target_ver == active_version) return;

    active_version = target_ver;
    create_sound_screen(active_version);
}

static void build_version_a(lv_obj_t * list) {
    lv_obj_t * hero = make_card(list, 20, 8, 440, 76);
    lv_obj_set_style_bg_color(hero, PRIMARY_CONTAINER, LV_PART_MAIN);
    char hero_sub[64];
    snprintf(hero_sub, sizeof(hero_sub), "Głośność %d%% • %s", master_volume, muted ? "wyciszony" : "aktywny");
    make_label(hero, "Proste sterowanie", 16, 10, ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    make_label(hero, hero_sub, 16, 44, lv_color_hex(0x8BD7DF), &lv_font_montserrat_14_pl);
    lv_obj_t * badge = lv_obj_create(hero);
    lv_obj_set_size(badge, 44, 44);
    lv_obj_set_pos(badge, 380, 16);
    make_surface(badge, lv_color_hex(0x116872), 22);
    lv_obj_t * glyph = make_label(badge, ICON_VOLUME, 0, 0, ON_PRIMARY_CONTAINER, &lv_font_control_icons_24);
    lv_obj_center(glyph);

    lv_obj_t * vol_card = make_card(list, 20, 96, 440, 126);
    make_label(vol_card, "Głośność głośnika", 16, 14, ON_SURFACE, &lv_font_montserrat_20_pl);
    char vol_txt[16];
    snprintf(vol_txt, sizeof(vol_txt), "%d%%", master_volume);
    volume_value_label = make_label(vol_card, vol_txt, 0, 16, PRIMARY, &lv_font_montserrat_20_pl);
    lv_obj_align(volume_value_label, LV_ALIGN_TOP_RIGHT, -16, 16);
    make_volume_slider(vol_card, 77);

    lv_obj_t * mute_card = make_card(list, 20, 234, 440, 76);
    make_label(mute_card, "Wycisz głośnik", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_label(mute_card, "Wyłącza wszystkie dźwięki panelu", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    make_switch(mute_card, 24, &muted);

    lv_obj_t * touch_card = make_card(list, 20, 322, 440, 76);
    make_label(touch_card, "Dźwięk dotyku", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_label(touch_card, "Krótki sygnał po naciśnięciu", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    make_switch(touch_card, 24, &touch_sound);

    lv_obj_t * notify_card = make_card(list, 20, 410, 440, 76);
    make_label(notify_card, "Powiadomienia Home Assistant", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_label(notify_card, "Sygnał dla zdarzeń i komunikatów", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    make_switch(notify_card, 24, &ha_notifications);

    lv_obj_t * test_card = make_card(list, 20, 498, 440, 76);
    make_test_button(test_card, 16);

    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 586);
    make_surface(spacer, SURFACE, 0);
}

static void build_version_b(lv_obj_t * list) {
    lv_obj_t * hero = make_card(list, 20, 8, 440, 76);
    lv_obj_set_style_bg_color(hero, PRIMARY_CONTAINER, LV_PART_MAIN);
    const char * profiles[] = {"Dom", "Cisza", "Noc", "Wyciszony"};
    char hero_sub[64];
    snprintf(hero_sub, sizeof(hero_sub), "Tryb: %s • Głośność %d%%", profiles[profile_index], master_volume);
    make_label(hero, "Tryby domu", 16, 10, ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    make_label(hero, hero_sub, 16, 44, lv_color_hex(0x8BD7DF), &lv_font_montserrat_14_pl);

    lv_obj_t * badge = lv_obj_create(hero);
    lv_obj_set_size(badge, 44, 44);
    lv_obj_set_pos(badge, 380, 16);
    make_surface(badge, lv_color_hex(0x116872), 22);
    lv_obj_t * glyph = make_label(badge, ICON_VOLUME, 0, 0, ON_PRIMARY_CONTAINER, &lv_font_control_icons_24);
    lv_obj_center(glyph);

    lv_obj_t * prof_card = make_card(list, 20, 96, 440, 120);
    make_label(prof_card, "Wybierz tryb", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    const char * prof_names[] = {"Dom", "Cisza", "Noc", "Wycisz"};
    for (int i = 0; i < 4; ++i) {
        bool sel = (i == profile_index);
        lv_obj_t * btn = lv_btn_create(prof_card);
        lv_obj_set_size(btn, 94, 40);
        lv_obj_set_pos(btn, 16 + i * 102, 62);
        make_surface(btn, sel ? PRIMARY_CONTAINER : SURFACE_HIGH, 20);
        if (!sel) {
            lv_obj_set_style_border_color(btn, OUTLINE_VARIANT, LV_PART_MAIN);
            lv_obj_set_style_border_width(btn, 1, LV_PART_MAIN);
        }
        lv_obj_t * lbl = make_label(btn, prof_names[i], 0, 0, sel ? ON_PRIMARY_CONTAINER : ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
        lv_obj_center(lbl);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(btn, profile_event_cb, LV_EVENT_CLICKED, (void *)(long)i);
    }

    lv_obj_t * vol_card = make_card(list, 20, 228, 440, 116);
    make_label(vol_card, "Głośność wybranego trybu", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    char vol_txt[16];
    snprintf(vol_txt, sizeof(vol_txt), "%d%%", master_volume);
    volume_value_label = make_label(vol_card, vol_txt, 0, 12, PRIMARY, &lv_font_montserrat_20_pl);
    lv_obj_align(volume_value_label, LV_ALIGN_TOP_RIGHT, -16, 12);
    make_volume_slider(vol_card, 68);

    lv_obj_t * quiet_card = make_card(list, 20, 356, 440, 92);
    make_label(quiet_card, "Automatyczna cisza nocna", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_label(quiet_card, "22:00–07:00 • przełącz na tryb Noc", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    make_switch(quiet_card, 32, &quiet_hours);

    lv_obj_t * alarm_card = make_card(list, 20, 460, 440, 92);
    make_label(alarm_card, "Alarmy także w ciszy", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_label(alarm_card, "Dym, zalanie i alarm bezpieczeństwa", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    make_switch(alarm_card, 32, &critical_alerts);

    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 564);
    make_surface(spacer, SURFACE, 0);
}

static void build_version_c(lv_obj_t * list) {
    lv_obj_t * vol_card = make_card(list, 20, 8, 440, 116);
    make_label(vol_card, "Głośność", 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    char vol_txt[16];
    snprintf(vol_txt, sizeof(vol_txt), "%d%%", master_volume);
    volume_value_label = make_label(vol_card, vol_txt, 0, 12, PRIMARY, &lv_font_montserrat_20_pl);
    lv_obj_align(volume_value_label, LV_ALIGN_TOP_RIGHT, -16, 12);
    make_volume_slider(vol_card, 68);

    lv_obj_t * quiet_card = make_card(list, 20, 136, 440, 70);
    make_label(quiet_card, "Tryb cichy", 16, 23, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_switch(quiet_card, 21, &quiet_hours);

    lv_obj_t * priority_card = make_card(list, 20, 218, 440, 70);
    make_label(priority_card, "Alarmy", 16, 23, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_switch(priority_card, 21, &critical_alerts);

    lv_obj_t * notify_card = make_card(list, 20, 300, 440, 70);
    make_label(notify_card, "Powiadomienia Home Assistant", 16, 23, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_switch(notify_card, 21, &ha_notifications);

    lv_obj_t * touch_card = make_card(list, 20, 382, 440, 70);
    make_label(touch_card, "Dźwięki dotyku", 16, 23, ON_SURFACE, &lv_font_montserrat_20_pl);
    make_switch(touch_card, 21, &touch_sound);

    lv_obj_t * spacer = lv_obj_create(list);
    lv_obj_set_size(spacer, 1, 20);
    lv_obj_set_pos(spacer, 0, 464);
    make_surface(spacer, SURFACE, 0);
}

static void create_sound_screen(int version) {
    if (screen) lv_obj_del(screen);

    screen = lv_obj_create(lv_layer_top());
    lv_obj_set_size(screen, 480, 480);
    lv_obj_set_pos(screen, 0, 0);
    make_surface(screen, SURFACE, 0);

    // Header bar
    lv_obj_t * header = lv_obj_create(screen);
    lv_obj_set_size(header, 480, 70);
    lv_obj_set_pos(header, 0, 0);
    make_surface(header, SURFACE, 0);

    lv_obj_t * back = lv_btn_create(header);
    lv_obj_set_size(back, 48, 48);
    lv_obj_set_pos(back, 12, 11);
    make_surface(back, SURFACE_HIGH, 24);
    lv_obj_t * back_icon = make_label(back, ICON_BACK, 0, 0, ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_icon);

    make_label(header, version == 2 ? "Dźwięki" : "Dźwięk", 76, 22, ON_SURFACE, &lv_font_montserrat_24_pl);

    // Compact selector leaves enough room for the Polish screen title.
    ver_a_btn = lv_btn_create(header);
    lv_obj_set_size(ver_a_btn, 48, 36);
    lv_obj_set_pos(ver_a_btn, 304, 17);
    make_surface(ver_a_btn, version == 0 ? PRIMARY_CONTAINER : SURFACE_HIGH, 18);
    if (version != 0) {
        lv_obj_set_style_border_color(ver_a_btn, OUTLINE_VARIANT, LV_PART_MAIN);
        lv_obj_set_style_border_width(ver_a_btn, 1, LV_PART_MAIN);
    }
    lv_obj_t * ver_a_lbl = make_label(ver_a_btn, "A", 0, 0, version == 0 ? ON_PRIMARY_CONTAINER : ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_center(ver_a_lbl);
    lv_obj_clear_flag(ver_a_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ver_a_btn, version_switch_event_cb, LV_EVENT_CLICKED, (void *)(long)0);

    ver_b_btn = lv_btn_create(header);
    lv_obj_set_size(ver_b_btn, 48, 36);
    lv_obj_set_pos(ver_b_btn, 360, 17);
    make_surface(ver_b_btn, version == 1 ? PRIMARY_CONTAINER : SURFACE_HIGH, 18);
    if (version != 1) {
        lv_obj_set_style_border_color(ver_b_btn, OUTLINE_VARIANT, LV_PART_MAIN);
        lv_obj_set_style_border_width(ver_b_btn, 1, LV_PART_MAIN);
    }
    lv_obj_t * ver_b_lbl = make_label(ver_b_btn, "B", 0, 0, version == 1 ? ON_PRIMARY_CONTAINER : ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_center(ver_b_lbl);
    lv_obj_clear_flag(ver_b_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ver_b_btn, version_switch_event_cb, LV_EVENT_CLICKED, (void *)(long)1);

    ver_c_btn = lv_btn_create(header);
    lv_obj_set_size(ver_c_btn, 48, 36);
    lv_obj_set_pos(ver_c_btn, 416, 17);
    make_surface(ver_c_btn, version == 2 ? PRIMARY_CONTAINER : SURFACE_HIGH, 18);
    if (version != 2) {
        lv_obj_set_style_border_color(ver_c_btn, OUTLINE_VARIANT, LV_PART_MAIN);
        lv_obj_set_style_border_width(ver_c_btn, 1, LV_PART_MAIN);
    }
    lv_obj_t * ver_c_lbl = make_label(ver_c_btn, "C", 0, 0, version == 2 ? ON_PRIMARY_CONTAINER : ON_SURFACE_VARIANT, &lv_font_montserrat_14_pl);
    lv_obj_center(ver_c_lbl);
    lv_obj_clear_flag(ver_c_lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(ver_c_btn, version_switch_event_cb, LV_EVENT_CLICKED, (void *)(long)2);

    // List Container
    list_container = lv_obj_create(screen);
    lv_obj_set_size(list_container, 480, 410);
    lv_obj_set_pos(list_container, 0, 70);
    make_surface(list_container, SURFACE, 0);
    lv_obj_add_flag(list_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list_container, LV_SCROLLBAR_MODE_OFF);

    if (version == 0) {
        build_version_a(list_container);
    } else if (version == 1) {
        build_version_b(list_container);
    } else {
        build_version_c(list_container);
    }
}

#ifdef PC_SIMULATOR
extern uint32_t t_buf[480 * 480];
static void export_screen_raw(const char * filename) {
    for (int i = 0; i < 10; ++i) {
        lv_timer_handler();
        usleep(10000);
    }
    FILE * f = fopen(filename, "wb");
    if (f) {
        fwrite(t_buf, 1, 480 * 480 * 4, f);
        fclose(f);
        printf("[Export] Saved %s (%d bytes)\n", filename, 480 * 480 * 4);
    }
}
#endif

int main(int argc, char **argv) {
    bool do_export = false;
    std::string export_file = "/tmp/sound_screen.raw";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "A" || arg == "a") active_version = 0;
        else if (arg == "B" || arg == "b") active_version = 1;
        else if (arg == "C" || arg == "c") active_version = 2;
        else if (arg == "--export") do_export = true;
        else if (arg.find("--out=") == 0) export_file = arg.substr(6);
    }

    lv_init();
    if (!hal_display_init() || !hal_touch_init()) return 1;

    create_sound_screen(active_version);

    if (do_export) {
#ifdef PC_SIMULATOR
        export_screen_raw(export_file.c_str());
#endif
        hal_shutdown();
        return 0;
    }

    while (1) {
        lv_timer_handler();
        usleep(5000);
    }

    hal_shutdown();
    return 0;
}
