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
#define ICON_MIC        "\xEF\x84\xB0"

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
static bool wake_word_enabled = true;
static int wake_sensitivity_index = 0; // 0=Standard, 1=High, 2=Max
static int mic_gain_percent = 85;
static bool hpf_filter_enabled = true;
static bool mute_on_blank = true;
static bool mute_on_tts = true;

static lv_obj_t * sensitivity_buttons[3] = {NULL, NULL, NULL};
static lv_obj_t * gain_slider_label = NULL;
static lv_obj_t * test_level_bar = NULL;
static lv_obj_t * test_status_label = NULL;

static void surface(lv_obj_t * obj, lv_color_t color, int radius) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t * card(lv_obj_t * parent, int x, int y, int w, int h) {
    lv_obj_t * obj = lv_obj_create(parent);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_pos(obj, x, y);
    surface(obj, SURFACE_CONTAINER, 24);
    return obj;
}

static lv_obj_t * label(lv_obj_t * parent, const char * text, int x, int y, lv_color_t color, const lv_font_t * font) {
    lv_obj_t * obj = lv_label_create(parent);
    lv_label_set_text(obj, text);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_text_font(obj, font, LV_PART_MAIN);
    return obj;
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

static void toggle_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    bool * flag = static_cast<bool *>(lv_event_get_user_data(e));
    *flag = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
}

static lv_obj_t * switch_card(lv_obj_t * parent, int y, const char * title, const char * subtitle, bool * value) {
    lv_obj_t * obj = card(parent, 20, y, 440, 76);
    label(obj, title, 16, 12, ON_SURFACE, &lv_font_montserrat_20_pl);
    label(obj, subtitle, 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t * sw = lv_switch_create(obj);
    style_switch(sw);
    lv_obj_set_pos(sw, 374, 24);
    if (*value) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, toggle_cb, LV_EVENT_VALUE_CHANGED, value);
    return obj;
}

static void sensitivity_button_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    int index = (int)(long)lv_event_get_user_data(e);
    wake_sensitivity_index = index;
    for (int i = 0; i < 3; ++i) {
        if (!sensitivity_buttons[i]) continue;
        bool sel = (i == index);
        surface(sensitivity_buttons[i], sel ? PRIMARY_CONTAINER : SURFACE_HIGH, 20);
        lv_obj_set_style_border_width(sensitivity_buttons[i], sel ? 0 : 1, LV_PART_MAIN);
    }
}

static void gain_slider_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) return;
    mic_gain_percent = lv_slider_get_value(lv_event_get_target(e));
    char text[16];
    snprintf(text, sizeof(text), "%d%%", mic_gain_percent);
    lv_label_set_text(gain_slider_label, text);
}

static void create_microphone_screen(void) {
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_scr_load(screen);
    } else {
        lv_obj_clean(screen);
    }
    surface(screen, SURFACE, 0);

    // Subscreen Header
    lv_obj_t * header = card(screen, 0, 0, 480, 72);
    surface(header, SURFACE, 0);
    lv_obj_t * back_btn = lv_btn_create(header);
    lv_obj_set_size(back_btn, 48, 48);
    lv_obj_set_pos(back_btn, 16, 12);
    surface(back_btn, SURFACE_HIGH, 24);
    lv_obj_t * back_ic = label(back_btn, ICON_BACK, 0, 0, ON_SURFACE, &lv_font_control_icons_24);
    lv_obj_center(back_ic);
    label(header, "Mikrofon", 76, 22, ON_SURFACE, &lv_font_montserrat_24_pl);

    // Scrollable List
    lv_obj_t * list = lv_obj_create(screen);
    lv_obj_set_size(list, 480, 408);
    lv_obj_set_pos(list, 0, 72);
    surface(list, SURFACE, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_AUTO);

    // Hero Banner
    lv_obj_t * hero = card(list, 20, 8, 440, 76);
    surface(hero, PRIMARY_CONTAINER, 24);
    label(hero, ICON_MIC, 20, 20, ON_PRIMARY_CONTAINER, &lv_font_control_icons_24);
    label(hero, "Mikrofon i Wybudzanie", 60, 14, ON_PRIMARY_CONTAINER, &lv_font_montserrat_20_pl);
    label(hero, "microWakeWord • ALC • Tłumienie szumów", 60, 44, ON_PRIMARY_CONTAINER, &lv_font_montserrat_14);

    // Card 1: Wake Word "Okay Nabu"
    lv_obj_t * wake_card = card(list, 20, 96, 440, 148);
    label(wake_card, "Wybudzanie \"Okay Nabu\"", 16, 14, ON_SURFACE, &lv_font_montserrat_20_pl);
    label(wake_card, "Lokalna detekcja frazy kluczowej", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14);
    lv_obj_t * wake_sw = lv_switch_create(wake_card);
    style_switch(wake_sw);
    lv_obj_set_pos(wake_sw, 374, 20);
    if (wake_word_enabled) lv_obj_add_state(wake_sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(wake_sw, toggle_cb, LV_EVENT_VALUE_CHANGED, &wake_word_enabled);

    label(wake_card, "Czułość wybudzania:", 16, 88, ON_SURFACE_VARIANT, &lv_font_montserrat_12_pl);
    const char * sens_labels[] = {"Standardowa", "Wysoka", "Maksymalna"};
    for (int i = 0; i < 3; ++i) {
        bool sel = (i == wake_sensitivity_index);
        sensitivity_buttons[i] = lv_btn_create(wake_card);
        lv_obj_set_size(sensitivity_buttons[i], 128, 38);
        lv_obj_set_pos(sensitivity_buttons[i], 16 + i * 136, 102);
        surface(sensitivity_buttons[i], sel ? PRIMARY_CONTAINER : SURFACE_HIGH, 19);
        lv_obj_set_style_border_color(sensitivity_buttons[i], OUTLINE_VARIANT, LV_PART_MAIN);
        lv_obj_set_style_border_width(sensitivity_buttons[i], sel ? 0 : 1, LV_PART_MAIN);
        lv_obj_add_event_cb(sensitivity_buttons[i], sensitivity_button_cb, LV_EVENT_CLICKED, (void *)(long)i);
        lv_obj_t * lbl = label(sensitivity_buttons[i], sens_labels[i], 0, 0, sel ? ON_PRIMARY_CONTAINER : ON_SURFACE, &lv_font_montserrat_12_pl);
        lv_obj_center(lbl);
    }

    // Card 2: Mic Gain / ALC
    lv_obj_t * gain_card = card(list, 20, 256, 440, 126);
    label(gain_card, "Czułość mikrofonu (Gain)", 16, 14, ON_SURFACE, &lv_font_montserrat_20_pl);
    char gain_text[16];
    snprintf(gain_text, sizeof(gain_text), "%d%%", mic_gain_percent);
    gain_slider_label = label(gain_card, gain_text, 0, 14, PRIMARY, &lv_font_montserrat_20_pl);
    lv_obj_align(gain_slider_label, LV_ALIGN_TOP_RIGHT, -16, 14);

    lv_obj_t * slider = lv_slider_create(gain_card);
    lv_obj_set_size(slider, 408, 20);
    lv_obj_set_pos(slider, 16, 77);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, mic_gain_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, PRIMARY, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, ON_PRIMARY_CONTAINER, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, gain_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Card 3: Switches
    switch_card(list, 394, "Filtr górnoprzepustowy (HPF)", "Tłumienie szumów otoczenia", &hpf_filter_enabled);
    switch_card(list, 482, "Wycisz przy wygaszonym ekranie", "Ochrona prywatności w trybie uśpienia", &mute_on_blank);
    switch_card(list, 570, "Wycisz podczas mowy TTS", "Ochrona przed samowybudzaniem", &mute_on_tts);

    // Card 4: Test Mic
    lv_obj_t * test_card = card(list, 20, 658, 440, 116);
    label(test_card, "Test mikrofonu", 16, 14, ON_SURFACE, &lv_font_montserrat_20_pl);
    test_status_label = label(test_card, "Sygnał wejściowy", 16, 44, ON_SURFACE_VARIANT, &lv_font_montserrat_14);

    test_level_bar = lv_bar_create(test_card);
    lv_obj_set_size(test_level_bar, 408, 14);
    lv_obj_set_pos(test_level_bar, 16, 78);
    lv_bar_set_range(test_level_bar, 0, 100);
    lv_bar_set_value(test_level_bar, 38, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(test_level_bar, OUTLINE_VARIANT, LV_PART_MAIN);
    lv_obj_set_style_bg_color(test_level_bar, PRIMARY, LV_PART_INDICATOR);

    lv_obj_t * spacer = card(list, 0, 786, 1, 20);
    surface(spacer, SURFACE, 0);
}

int main(void) {
    lv_init();
    if (!hal_display_init() || !hal_touch_init()) return 1;
    create_microphone_screen();
    while (1) {
        lv_timer_handler();
        usleep(5000);
    }
    hal_shutdown();
    return 0;
}
