#include <stdio.h>
#include <unistd.h>
#include <string>

#include "../lvgl/lvgl.h"

bool hal_display_init(void);
bool hal_touch_init(void);
void hal_shutdown(void);

LV_FONT_DECLARE(lv_font_montserrat_12_pl);
LV_FONT_DECLARE(lv_font_montserrat_14_pl);
LV_FONT_DECLARE(lv_font_montserrat_16_pl);
LV_FONT_DECLARE(lv_font_montserrat_20_pl);
LV_FONT_DECLARE(lv_font_montserrat_24_pl);

enum DemoState {
    DEMO_LISTENING,
    DEMO_TRANSCRIPT,
    DEMO_RESPONSE,
    DEMO_SPEAKING,
    DEMO_ERROR,
    DEMO_STATE_COUNT
};

static lv_obj_t * screen = NULL;
static lv_obj_t * status_label = NULL;
static lv_obj_t * text_label = NULL;
static lv_obj_t * user_label = NULL;
static lv_obj_t * response_label = NULL;
static lv_obj_t * state_button_label = NULL;
static lv_obj_t * edge_glow = NULL;
static lv_obj_t * edge_primary = NULL;
static lv_obj_t * aurora_bars[3] = {NULL, NULL, NULL};
static lv_obj_t * edge_segments[14] = {NULL};
static lv_timer_t * animation_timer = NULL;
static int active_version = 2;
static int demo_states[3] = {DEMO_LISTENING, DEMO_LISTENING, DEMO_LISTENING};
static uint32_t animation_started = 0;
static uint32_t response_started = 0;
static size_t response_revealed_characters = 0;
static const char * TEST_USER_TEXT =
    "Włącz lampę w salonie i ustaw jasność na trzydzieści procent.";
static const char * TEST_RESPONSE_TEXT =
    "Włączyłem lampę w salonie i ustawiłem jasność na 30%.";

static const lv_color_t BACKGROUND = lv_color_hex(0x0B1017);
static const lv_color_t SURFACE = lv_color_hex(0x18202A);
static const lv_color_t TEXT = lv_color_hex(0xEDF4F6);
static const lv_color_t MUTED = lv_color_hex(0x9BAAB0);

static lv_color_t state_color(int state) {
    if (state == DEMO_TRANSCRIPT) return lv_color_hex(0x62A9FF);
    if (state == DEMO_RESPONSE) return lv_color_hex(0x66D69A);
    if (state == DEMO_SPEAKING) return lv_color_hex(0xA58BFF);
    if (state == DEMO_ERROR) return lv_color_hex(0xE46E7D);
    return lv_color_hex(0x4FD8E6);
}

static const char * state_name(int state) {
    if (state == DEMO_TRANSCRIPT) return "USŁYSZAŁEM";
    if (state == DEMO_RESPONSE) return "ODPOWIEDŹ";
    if (state == DEMO_SPEAKING) return "ODPOWIADAM";
    if (state == DEMO_ERROR) return "NIE UDAŁO SIĘ";
    return "SŁUCHAM";
}

static const char * state_text(int state) {
    if (state == DEMO_TRANSCRIPT) {
        return "Włącz lampę w salonie i ustaw jasność na trzydzieści procent.";
    }
    if (state == DEMO_RESPONSE) {
        return "Włączyłem lampę w salonie i ustawiłem jasność na 30%.";
    }
    if (state == DEMO_SPEAKING) {
        return "Lampa jest już włączona. Mogę również zmienić jej kolor albo ustawić czas automatycznego wyłączenia.";
    }
    if (state == DEMO_ERROR) {
        return "Nie udało się połączyć z Home Assistant. Spróbuj ponownie.";
    }
    return "Powiedz, czego potrzebujesz.";
}

static size_t utf8_prefix_bytes(const char * text, size_t characters) {
    size_t bytes = 0;
    size_t count = 0;
    while (text[bytes] && count < characters) {
        const unsigned char value = static_cast<unsigned char>(text[bytes]);
        size_t width = 1;
        if ((value & 0xE0) == 0xC0) width = 2;
        else if ((value & 0xF0) == 0xE0) width = 3;
        else if ((value & 0xF8) == 0xF0) width = 4;
        bytes += width;
        ++count;
    }
    return bytes;
}

static size_t utf8_character_count(const char * text) {
    size_t count = 0;
    for (size_t i = 0; text[i]; ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) ++count;
    }
    return count;
}

static void surface(lv_obj_t * obj, lv_color_t color, int radius) {
    lv_obj_set_style_bg_color(obj, color, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(obj, radius, LV_PART_MAIN);
    lv_obj_set_style_pad_all(obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t * label(lv_obj_t * parent, const char * text, const lv_font_t * font,
                        lv_color_t color) {
    lv_obj_t * result = lv_label_create(parent);
    lv_label_set_text(result, text);
    lv_obj_set_style_text_font(result, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(result, color, LV_PART_MAIN);
    return result;
}

static void update_state(void) {
    const int state = demo_states[active_version];
    const lv_color_t color = state_color(state);
    lv_label_set_text(status_label, state_name(state));
    lv_obj_set_style_text_color(status_label, color, LV_PART_MAIN);
    lv_label_set_text(state_button_label, state == DEMO_ERROR ? "Wróć do nasłuchu" : "Następny stan");
    animation_started = lv_tick_get();
    response_started = animation_started;
    response_revealed_characters = 0;

    if (state == DEMO_LISTENING || state == DEMO_ERROR) {
        lv_obj_clear_flag(text_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(text_label, state_text(state));
        lv_obj_add_flag(user_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(response_label, LV_OBJ_FLAG_HIDDEN);
    } else if (state == DEMO_TRANSCRIPT) {
        lv_obj_add_flag(text_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(user_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(response_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(user_label, TEST_USER_TEXT);
        lv_obj_set_y(user_label, 166);
    } else {
        lv_obj_add_flag(text_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(user_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(response_label, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(user_label, TEST_USER_TEXT);
        lv_obj_set_y(user_label, state == DEMO_SPEAKING ? 94 : 166);
        lv_label_set_text(response_label, state == DEMO_SPEAKING ? TEST_RESPONSE_TEXT : "");
    }

    if (edge_glow) lv_obj_set_style_bg_color(edge_glow, color, LV_PART_MAIN);
    if (edge_primary) {
        lv_obj_set_style_bg_color(edge_primary, color, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(edge_primary, lv_color_hex(0x8C7CFF), LV_PART_MAIN);
    }
    for (int i = 0; i < 3; ++i) {
        if (aurora_bars[i]) lv_obj_set_style_bg_color(aurora_bars[i], color, LV_PART_MAIN);
    }
    for (int i = 0; i < 14; ++i) {
        if (edge_segments[i]) lv_obj_set_style_bg_color(edge_segments[i], color, LV_PART_MAIN);
    }
}

static void state_button_cb(lv_event_t * e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    demo_states[active_version] = (demo_states[active_version] + 1) % DEMO_STATE_COUNT;
    update_state();
}

static void animation_cb(lv_timer_t * timer) {
    (void)timer;
    const uint32_t elapsed = lv_tick_get() - animation_started;
    const int state = demo_states[active_version];
    const int speed = state == DEMO_SPEAKING ? 7 : (state == DEMO_LISTENING ? 11 : 15);

    if (state == DEMO_RESPONSE) {
        const uint32_t transition = lv_tick_get() - response_started;
        const int user_y = transition >= 420 ? 94 : 166 - static_cast<int>(transition * 72 / 420);
        lv_obj_set_y(user_label, user_y);
        const size_t characters = std::min<size_t>(transition / 34,
                                                    utf8_character_count(TEST_RESPONSE_TEXT));
        if (characters != response_revealed_characters) {
            response_revealed_characters = characters;
            const size_t bytes = utf8_prefix_bytes(TEST_RESPONSE_TEXT, characters);
            const std::string visible(TEST_RESPONSE_TEXT, bytes);
            lv_label_set_text(response_label, visible.c_str());
        }
    }

    if (active_version == 0) {
        const int travel = static_cast<int>((elapsed / speed) % 640);
        const int x = travel - 160;
        const int width = state == DEMO_SPEAKING ? 220 : 150;
        lv_obj_set_width(edge_primary, width);
        lv_obj_set_x(edge_primary, x);
        const int pulse = static_cast<int>((elapsed % 1000) * 200 / 1000);
        lv_obj_set_style_bg_opa(edge_glow, 20 + (pulse <= 100 ? pulse : 200 - pulse) / 3,
                                LV_PART_MAIN);
    } else if (active_version == 1) {
        for (int i = 0; i < 3; ++i) {
            const int phase = static_cast<int>((elapsed + i * 360) % 1500);
            const int triangle = phase <= 750 ? phase : 1500 - phase;
            const int width = 180 + triangle * (90 + i * 12) / 750;
            const int x = (480 - width) / 2 + (i - 1) * 26;
            lv_obj_set_width(aurora_bars[i], width);
            lv_obj_set_x(aurora_bars[i], x);
            lv_obj_set_y(aurora_bars[i], 466 - i * 5 - triangle * 5 / 750);
            lv_obj_set_style_bg_opa(aurora_bars[i], 58 + i * 35, LV_PART_MAIN);
        }
    } else {
        for (int i = 0; i < 14; ++i) {
            const int phase = static_cast<int>((elapsed / 7 + i * 23) % 100);
            const int triangle = phase <= 50 ? phase : 100 - phase;
            int height = 4 + triangle * (state == DEMO_SPEAKING ? 24 : 14) / 50;
            if (state == DEMO_TRANSCRIPT || state == DEMO_RESPONSE) height = 7;
            lv_obj_set_height(edge_segments[i], height);
            lv_obj_set_y(edge_segments[i], 480 - height);
            lv_obj_set_style_bg_opa(edge_segments[i], 100 + triangle * 3, LV_PART_MAIN);
        }
    }
}

static void create_version_tabs(lv_obj_t * parent) {
    lv_obj_t * title = label(parent, "ASSIST  •  SEGMENTY", &lv_font_montserrat_12_pl, MUTED);
    lv_obj_set_width(title, 408);
    lv_obj_set_pos(title, 36, 24);
    lv_obj_set_style_text_letter_space(title, 2, LV_PART_MAIN);
}

static void create_edge_variant(lv_obj_t * parent, int version) {
    if (version == 0) {
        edge_glow = lv_obj_create(parent);
        lv_obj_set_size(edge_glow, 480, 28);
        lv_obj_set_pos(edge_glow, 0, 452);
        surface(edge_glow, state_color(demo_states[version]), 0);
        lv_obj_set_style_bg_opa(edge_glow, 28, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_color(edge_glow, BACKGROUND, LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(edge_glow, LV_GRAD_DIR_VER, LV_PART_MAIN);

        edge_primary = lv_obj_create(parent);
        lv_obj_set_size(edge_primary, 150, 7);
        lv_obj_set_pos(edge_primary, 0, 473);
        surface(edge_primary, state_color(demo_states[version]), 4);
        lv_obj_set_style_bg_grad_color(edge_primary, lv_color_hex(0x8C7CFF), LV_PART_MAIN);
        lv_obj_set_style_bg_grad_dir(edge_primary, LV_GRAD_DIR_HOR, LV_PART_MAIN);
    } else if (version == 1) {
        for (int i = 0; i < 3; ++i) {
            aurora_bars[i] = lv_obj_create(parent);
            lv_obj_set_size(aurora_bars[i], 210, 5 + i * 2);
            lv_obj_set_pos(aurora_bars[i], 135, 466 - i * 5);
            surface(aurora_bars[i], state_color(demo_states[version]), 6);
            lv_obj_set_style_bg_grad_color(aurora_bars[i],
                                            i == 1 ? lv_color_hex(0x8C7CFF) : lv_color_hex(0x205C68),
                                            LV_PART_MAIN);
            lv_obj_set_style_bg_grad_dir(aurora_bars[i], LV_GRAD_DIR_HOR, LV_PART_MAIN);
        }
    } else {
        for (int i = 0; i < 14; ++i) {
            edge_segments[i] = lv_obj_create(parent);
            lv_obj_set_size(edge_segments[i], 22, 8);
            lv_obj_set_pos(edge_segments[i], 8 + i * 34, 472);
            surface(edge_segments[i], state_color(demo_states[version]), 6);
        }
    }
}

static void create_screen(int version) {
    if (!screen) {
        screen = lv_obj_create(NULL);
        lv_scr_load(screen);
    } else {
        lv_obj_clean(screen);
    }
    edge_glow = NULL;
    edge_primary = NULL;
    for (int i = 0; i < 3; ++i) aurora_bars[i] = NULL;
    for (int i = 0; i < 14; ++i) edge_segments[i] = NULL;
    surface(screen, BACKGROUND, 0);
    create_version_tabs(screen);

    status_label = label(screen, state_name(demo_states[version]), &lv_font_montserrat_14_pl,
                         state_color(demo_states[version]));
    lv_obj_set_width(status_label, 408);
    lv_obj_set_pos(status_label, 36, 62);
    lv_obj_set_style_text_letter_space(status_label, 2, LV_PART_MAIN);

    text_label = label(screen, state_text(demo_states[version]), &lv_font_montserrat_24_pl, TEXT);
    lv_obj_set_width(text_label, 408);
    lv_obj_set_pos(text_label, 36, 110);
    lv_label_set_long_mode(text_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(text_label, 8, LV_PART_MAIN);

    user_label = label(screen, TEST_USER_TEXT, &lv_font_montserrat_20_pl, lv_color_hex(0xB8CAD0));
    lv_obj_set_width(user_label, 360);
    lv_obj_set_pos(user_label, 84, 166);
    lv_obj_set_style_text_align(user_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(user_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(user_label, LV_OBJ_FLAG_HIDDEN);

    response_label = label(screen, "", &lv_font_montserrat_24_pl, TEXT);
    lv_obj_set_width(response_label, 408);
    lv_obj_set_pos(response_label, 36, 210);
    lv_obj_set_style_text_align(response_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_label_set_long_mode(response_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_line_space(response_label, 8, LV_PART_MAIN);
    lv_obj_add_flag(response_label, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t * state_button = lv_btn_create(screen);
    lv_obj_set_size(state_button, 200, 44);
    lv_obj_set_pos(state_button, 140, 386);
    surface(state_button, SURFACE, 22);
    lv_obj_set_style_border_width(state_button, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(state_button, lv_color_hex(0x34434D), LV_PART_MAIN);
    lv_obj_add_event_cb(state_button, state_button_cb, LV_EVENT_CLICKED, NULL);
    state_button_label = label(state_button, "Następny stan", &lv_font_montserrat_14_pl, TEXT);
    lv_obj_center(state_button_label);

    create_edge_variant(screen, version);
    if (animation_timer) lv_timer_del(animation_timer);
    animation_started = lv_tick_get();
    animation_timer = lv_timer_create(animation_cb, 16, NULL);
    update_state();
}

int main(void) {
    lv_init();
    if (!hal_display_init() || !hal_touch_init()) return 1;
    create_screen(2);
    while (1) {
        lv_timer_handler();
        usleep(5000);
    }
    hal_shutdown();
    return 0;
}
