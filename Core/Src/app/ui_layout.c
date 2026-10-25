#include "ui.h"
#include "key.h"
#include "app_service.h"
#include "lv_font_ui_font.h"
#include <stdio.h>
#include <string.h>

/* Chinese strings are kept as explicit UTF-8 escapes so the MDK toolchain
 * does not depend on the source file encoding. */
#define S_LID          "\xE7\x9B\x96:"
#define S_DOOR_CLOSED  "\xE5\xB7\xB2\xE5\x85\xB3"
#define S_DOOR_OPEN    "\xE5\xB7\xB2\xE5\xBC\x80"
#define S_DOOR_MOVING  "\xE5\x8A\xA8\xE4\xBD\x9C\xE4\xB8\xAD"
#define S_DOOR_FAULT   "\xE6\x95\x85\xE9\x9A\x9C"
#define S_HEAT         "\xE5\x8D\x87\xE6\xB8\xA9"
#define S_COOL         "\xE9\x99\x8D\xE6\xB8\xA9"
#define S_HOLD         "\xE6\x81\x92\xE6\xB8\xA9\xE4\xB8\xAD"
#define S_REACH        "\xE8\xBE\xBE\xE6\xA0\x87"
#define S_BUZZ         "\xE8\x9C\x82\xE9\xB8\xA3"
#define S_ALARM        "\xE6\x8A\xA5\xE8\xAD\xA6"
#define S_SAVE         "\xE4\xBF\x9D\xE5\xAD\x98"
#define S_TEMP_TITLE   "\xE6\xB8\xA9\xE5\xBA\xA6 TEMP"
#define S_TARGET_TITLE "\xE7\x9B\xAE\xE6\xA0\x87 TEMP"
#define S_TARGET_FMT   "\xE7\x9B\xAE\xE6\xA0\x87 %ld.%ld \xE2\x84\x83"
#define S_TIME_TITLE   "\xE5\xAD\xB5\xE8\x82\xB2\xE6\x97\xB6\xE9\x97\xB4 TIME"
#define S_WAIT         "\xE5\xBE\x85\xE5\xBC\x80\xE5\xA7\x8B"
#define S_DONE         "\xE5\xB7\xB2\xE7\xBB\x93\xE6\x9D\x9F"
#define S_PAUSED       "\xE5\xB7\xB2\xE6\x9A\x82\xE5\x81\x9C"
#define S_REMAIN_MIN   "\xE5\x89\xA9\xE4\xBD\x99 \xE5\x85\xB1 %lu.%lu min"
#define S_REMAIN_H     "\xE5\x89\xA9\xE4\xBD\x99 \xE5\x85\xB1 %lu.%lu h"
#define S_DELAY_TITLE  "\xE5\xBB\xB6\xE8\xBF\x9F 4\xE2\x84\x83 \xE4\xBF\x9D\xE5\xAD\x98"
#define S_EDITABLE     "\xE5\x8F\xAF\xE7\xBC\x96\xE8\xBE\x91"
#define S_AFTER        "%lu s \xE5\x90\x8E\xE9\x99\x8D\xE6\xB8\xA9"
#define S_COOLING      "\xE9\x99\x8D\xE6\xB8\xA9\xE4\xB8\xAD"
#define S_KEEP         "\xE4\xBF\x9D\xE5\xAD\x98\xE4\xB8\xAD \xE7\x9B\xB4\xE8\x87\xB3\xE5\x8F\x96\xE6\x9D\xBF"
#define S_START        "\xE5\xBC\x80\xE5\xA7\x8B"
#define S_STARTING     "\xE8\xAE\xA1\xE6\x97\xB6\xE4\xB8\xAD"
#define S_STOP         "\xE5\x81\x9C\xE6\xAD\xA2"
#define S_OPEN         "\xE5\xBC\x80\xE7\x9B\x96"
#define S_CLOSE        "\xE5\x85\xB3\xE7\x9B\x96"
#define S_MUTE         "\xE6\xB6\x88\xE9\x9F\xB3"
#define S_NOT_BUZZ     "\xE6\x9C\xAA\xE8\x9C\x82\xE9\xB8\xA3"
#define S_SET_FMT      "\xE8\xAE\xBE\xE5\xAE\x9A %ld.%ld"
#define S_COMM_ERROR   "\xE9\x80\x9A\xE8\xAE\xAF\xE5\xBC\x82\xE5\xB8\xB8"
#define S_TEMP_ERROR   "\xE6\xB8\xA9\xE6\x8E\xA7\xE5\xBC\x82\xE5\xB8\xB8"
#define S_LID_ERROR    "\xE5\xBC\x80\xE7\x9B\x96\xE5\xBC\x82\xE5\xB8\xB8"
#define S_SET_TEMP_MSG "\xE8\xAE\xBE\xE5\xAE\x9A\xE6\xB8\xA9\xE5\xBA\xA6"

/* Keep supplied bitmap data const; small descriptor copies add a Latin
 * fallback for spaces/punctuation absent from the supplied subset. */
static lv_font_t s_font12, s_font14, s_font28;
#define F12 (&s_font12)
#define F14 (&s_font14)
#define F28 (&s_font28)

#define BG       0x081B12
#define HEADER   0x06140D
#define LINE     0x244B36
#define GREEN    0x6FFFA7
#define MUTED    0x739C88
#define DISABLED 0x446251
#define YELLOW   0xFFD54A
#define RED      0xFF7770
#define ORANGE   0xFFA54A
#define BLUE     0x5FC8FF

/* Cell geometry: 24 + 64 + 64 + 88 = 240. */
#define TEMP_Y   24
#define TEMP_H   64
#define TIME_Y   88
#define TIME_H   64
#define DELAY_Y  152
#define DELAY_H  88
#define BAR_W    184

/* UI owns handles/edit state only. No blocking serial transactions here. */
static lv_obj_t *s_pv, *s_target, *s_time, *s_time_aux, *s_delay;
static lv_obj_t *s_delay_set, *s_delay_state;
static lv_obj_t *s_alarm, *s_door, *s_phase, *s_sv;
static lv_obj_t *s_kt[4], *s_kn[4];       /* key titles / notes */
static lv_obj_t *s_ic_play[3], *s_ic_stop, *s_ic_door, *s_ic_mute[3];
static lv_obj_t *s_ic_up[3], *s_ic_down[3], *s_ic_dot;
static lv_obj_t *s_temp_bar, *s_time_bar, *s_delay_bar;
static lv_obj_t *s_focus, *s_digit;
static uint8_t s_ok, s_focus_index, s_edit, s_digit_index, s_focus_shown;
static int32_t s_draft;
static app_snapshot_t s_view;
static uint32_t s_refresh_ms;
static uint32_t s_repeat_key = KEY_COUNT, s_hold_ms, s_repeat_ms;
static uint8_t s_repeating;

static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h,
                       uint32_t color, int border)
{
    lv_obj_t *o = lv_obj_create(p);
    if(!o) { s_ok = 0; return NULL; }
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(LINE), 0);
    lv_obj_set_style_border_width(o, border, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *p, const char *v, int x, int y, int w, int h,
                      const lv_font_t *font, uint32_t color, lv_text_align_t align)
{
    lv_obj_t *o = lv_label_create(p);
    if(!o) { s_ok = 0; return NULL; }
    lv_obj_remove_style_all(o);
    lv_obj_set_style_text_font(o, font, 0);
    lv_obj_set_style_text_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_text_align(o, align, 0);
    lv_obj_set_style_text_letter_space(o, 0, 0);
    lv_obj_set_style_text_line_space(o, 0, 0);
    lv_label_set_long_mode(o, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_label_set_text(o, v);
    return o;
}

/* Avoid per-refresh allocation/invalidation when the visible value is unchanged. */
static void value(lv_obj_t *o, const char *v)
{
    if(o && strcmp(lv_label_get_text(o), v) != 0) lv_label_set_text(o, v);
}

static void tint(lv_obj_t *o, uint32_t c)
{
    if(o) lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}

static void bar(lv_obj_t *track, lv_obj_t *fill, uint32_t width, uint32_t color)
{
    if(!fill) return;
    if(!width) {
        lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(fill, (int32_t)width);
    lv_obj_set_style_bg_color(fill, lv_color_hex(color), 0);
}

/* 12:36 below one hour; 2:05:00 above. */
static void clock_text(char *b, size_t n, uint32_t sec)
{
    uint32_t h = sec / 3600u, m = (sec / 60u) % 60u;
    if(h) snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)h,
                   (unsigned long)m, (unsigned long)(sec % 60u));
    else snprintf(b, n, "%02lu:%02lu", (unsigned long)m,
                  (unsigned long)(sec % 60u));
}

static int allowed(uint8_t i)
{
    inc_phase_t ph = s_view.run.phase;
    switch(i) {
    case 0: return ph == INC_IDLE;                                  /* temperature */
    case 1: return ph == INC_IDLE;                                  /* time        */
    case 2: return ph == INC_IDLE || ph == INC_RUNNING;             /* delay       */
    case 3: return ph == INC_IDLE;                                  /* start       */
    case 4: return incubator_model_active(&s_view.run) ||
                   ph == INC_FINISHED || ph == INC_ERROR;           /* stop        */
    case 5: return s_view.door != MOTOR_OPENING &&
                   s_view.door != MOTOR_CLOSING &&
                   s_view.door != MOTOR_INSTALLING;                 /* lid         */
    case 6: return s_view.run.buzzer != 0;                          /* mute        */
    }
    return 0;
}

static void focus_update(void)
{
    static const int bounds[7][4] = {
        {2, TEMP_Y + 2, 196, TEMP_H - 4},
        {2, TIME_Y + 2, 196, TIME_H - 4},
        {2, DELAY_Y + 2, 196, DELAY_H - 4},
        {202, 25, 57, 70}, {261, 25, 58, 70},
        {202, 97, 57, 70}, {261, 97, 58, 70}
    };
    const int *b = bounds[s_focus_index];
    lv_obj_set_pos(s_focus, b[0], b[1]);
    lv_obj_set_size(s_focus, b[2], b[3]);
    lv_obj_set_style_border_color(s_focus, lv_color_hex(s_edit ? YELLOW : GREEN), 0);
    s_focus_shown = 1;
}

static void icon_state(int up, int down, uint32_t color)
{
    int i;
    for(i = 0; i < 3; ++i) {
        if(up) lv_obj_remove_flag(s_ic_up[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_ic_up[i], LV_OBJ_FLAG_HIDDEN);
        if(down) lv_obj_remove_flag(s_ic_down[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_ic_down[i], LV_OBJ_FLAG_HIDDEN);
        if(up || down) lv_obj_set_style_bg_color(s_ic_up[i], lv_color_hex(color), 0);
        if(up || down) lv_obj_set_style_bg_color(s_ic_down[i], lv_color_hex(color), 0);
    }
    if(s_ic_dot) {
        if(up || down) lv_obj_add_flag(s_ic_dot, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(s_ic_dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_ic_dot, lv_color_hex(color), 0);
        }
    }
}

static void key_icon_color(lv_obj_t *o, uint32_t c)
{
    if(o) lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
}

static void key_icons(uint32_t start_c, uint32_t stop_c,
                      uint32_t door_c, uint32_t mute_c)
{
    int i;
    for(i = 0; i < 3; ++i) {
        key_icon_color(s_ic_play[i], start_c);
        key_icon_color(s_ic_mute[i], mute_c);
    }
    key_icon_color(s_ic_stop, stop_c);
    if(s_ic_door) lv_obj_set_style_border_color(s_ic_door, lv_color_hex(door_c), 0);
}

void ui_layout_init(void)
{
    lv_obj_t *s = lv_screen_active();
    int i;
    s_font12 = lv_font_ui_font_12; s_font12.fallback = &lv_font_montserrat_14;
    s_font14 = lv_font_ui_font_14; s_font14.fallback = &lv_font_montserrat_14;
    s_font28 = lv_font_ui_font_28; s_font28.fallback = &lv_font_montserrat_20;
    s_ok = 1; s_focus_index = 0; s_edit = 0; s_digit_index = 0; s_focus_shown = 0;
    lv_obj_clean(s); lv_obj_remove_style_all(s);
    lv_obj_set_size(s, 320, 240);
    lv_obj_set_style_bg_color(s, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    /* Structure before text: no opaque overlay can hide a label. */
    rect(s, 0, 0, 320, 24, HEADER, 0);
    rect(s, 0, 23, 320, 1, LINE, 0);
    rect(s, 200, 24, 1, 216, LINE, 0);
    rect(s, 0, TIME_Y - 1, 200, 1, LINE, 0);
    rect(s, 0, DELAY_Y - 1, 200, 1, LINE, 0);
    rect(s, 201, 24, 59, 72, 0x143523, 1);
    rect(s, 260, 24, 60, 72, 0x102B1D, 1);
    rect(s, 201, 96, 59, 72, BG, 1);
    rect(s, 260, 96, 60, 72, 0x10251A, 1);

    rect(s, 8, TEMP_Y + 62, BAR_W, 2, 0x263C2F, 0);
    s_temp_bar = rect(s, 8, TEMP_Y + 62, 0, 2, GREEN, 0);
    rect(s, 8, TIME_Y + 59, BAR_W, 4, 0x263C2F, 0);
    s_time_bar = rect(s, 8, TIME_Y + 59, 0, 4, GREEN, 0);
    rect(s, 8, DELAY_Y + 84, BAR_W, 3, 0x263C2F, 0);
    s_delay_bar = rect(s, 8, DELAY_Y + 84, 0, 3, YELLOW, 0);

    /* Header phase icons (pixel triangles / dot). */
    s_ic_up[0] = rect(s, 107, 7, 2, 2, GREEN, 0);
    s_ic_up[1] = rect(s, 105, 10, 6, 2, GREEN, 0);
    s_ic_up[2] = rect(s, 104, 13, 8, 2, GREEN, 0);
    s_ic_down[0] = rect(s, 104, 7, 8, 2, BLUE, 0);
    s_ic_down[1] = rect(s, 105, 10, 6, 2, BLUE, 0);
    s_ic_down[2] = rect(s, 107, 13, 2, 2, BLUE, 0);
    s_ic_dot = rect(s, 106, 9, 4, 4, GREEN, 0);

    /* Key icons. */
    s_ic_play[0] = rect(s, 226, 32, 3, 12, GREEN, 0);
    s_ic_play[1] = rect(s, 229, 35, 3, 6, GREEN, 0);
    s_ic_play[2] = rect(s, 232, 37, 3, 2, GREEN, 0);
    s_ic_stop = rect(s, 285, 34, 9, 9, GREEN, 0);
    s_ic_door = rect(s, 283, 33, 13, 11, BG, 2);
    lv_obj_set_style_bg_opa(s_ic_door, LV_OPA_TRANSP, 0);
    s_ic_mute[0] = rect(s, 285, 36, 3, 4, GREEN, 0);
    s_ic_mute[1] = rect(s, 289, 32, 3, 8, GREEN, 0);
    s_ic_mute[2] = rect(s, 293, 28, 3, 12, GREEN, 0);

    s_focus = rect(s, 2, TEMP_Y + 2, 196, TEMP_H - 4, BG, 1);
    if(!s_ok) return;
    lv_obj_set_style_bg_opa(s_focus, LV_OPA_TRANSP, 0);
    s_digit = rect(s, 8, 72, 12, 2, YELLOW, 0);
    if(!s_ok) return;
    lv_obj_add_flag(s_digit, LV_OBJ_FLAG_HIDDEN);

    /* Header. */
    s_alarm = text(s, "ALM", 6, 4, 32, 16, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_door = text(s, S_LID S_DOOR_CLOSED, 42, 4, 62, 16, F12, GREEN, LV_TEXT_ALIGN_LEFT);
    s_phase = text(s, S_WAIT, 116, 4, 70, 16, F12, MUTED, LV_TEXT_ALIGN_CENTER);
    s_sv = text(s, "", 188, 4, 126, 16, F12, MUTED, LV_TEXT_ALIGN_RIGHT);

    /* Temperature cell. */
    text(s, S_TEMP_TITLE, 8, 26, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_pv = text(s, "--.-", 8, 40, 106, 32, F28, GREEN, LV_TEXT_ALIGN_LEFT);
    text(s, "\xE2\x84\x83", 118, 52, 24, 16, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_target = text(s, "", 8, 72, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);

    /* Time cell. */
    text(s, S_TIME_TITLE, 8, 89, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_time = text(s, "15.0 min", 8, 103, 184, 31, F28, 0xD2ECDD, LV_TEXT_ALIGN_LEFT);
    s_time_aux = text(s, S_WAIT, 8, 134, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);

    /* Delay cell. */
    text(s, S_DELAY_TITLE, 8, 153, 124, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_delay_set = text(s, "", 140, 153, 52, 14, F12, MUTED, LV_TEXT_ALIGN_RIGHT);
    s_delay = text(s, "0", 8, 170, 60, 34, F28, YELLOW, LV_TEXT_ALIGN_LEFT);
    text(s, "s", 74, 185, 14, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_delay_state = text(s, S_EDITABLE, 8, 207, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);

    /* Four action keys: start, stop, lid, mute. */
    for(i = 0; i < 4; ++i) {
        int x = (i == 1 || i == 3) ? 260 : 201;
        int y = (i < 2) ? 24 : 96;
        s_kt[i] = text(s, "", x, y + 30, (i == 1 || i == 3) ? 60 : 59, 20,
                       F14, MUTED, LV_TEXT_ALIGN_CENTER);
        s_kn[i] = text(s, "", x, y + 52, (i == 1 || i == 3) ? 60 : 59, 14,
                       F12, MUTED, LV_TEXT_ALIGN_CENTER);
    }
    if(!s_ok) return;
    focus_update();
    s_refresh_ms = HAL_GetTick() - 100;
    ui_layout_process();
}

int ui_layout_ready(void) { return s_ok; }

static void nav(uint32_t key)
{
    static const uint8_t right[7] = {3, 5, 6, 4, 0, 6, 2};
    static const uint8_t left[7]  = {4, 6, 6, 0, 3, 1, 5};
    static const uint8_t up[7]    = {2, 0, 1, 5, 6, 3, 4};
    static const uint8_t down[7]  = {1, 2, 0, 5, 6, 3, 4};
    const uint8_t *map = key == KEY_RIGHT ? right : key == KEY_LEFT ? left :
                         key == KEY_UP ? up : down;
    uint8_t next = map[s_focus_index];
    for(uint8_t i = 0; i < 7 && !allowed(next); ++i) next = map[next];
    if(allowed(next)) {
        s_focus_index = next;
        s_edit = 0;
        focus_update();
    }
}

void ui_layout_key(uint32_t key)
{
    if(!s_ok) return;
    if(!s_repeating && (key == KEY_UP || key == KEY_DOWN)) {
        s_repeat_key = key;
        s_hold_ms = s_repeat_ms = HAL_GetTick();
    }
    /* A running buzzer is silenced by SET from any focus position. */
    if(key == KEY_OK && s_view.run.buzzer) {
        app_service_submit(APP_MUTE, 0);
        return;
    }
    if(s_edit) {
        if(key == KEY_OK) {
            app_command_kind_t kind = s_focus_index == 0 ? APP_SET_TEMP :
                                      s_focus_index == 1 ? APP_SET_TIME : APP_SET_DELAY;
            if(app_service_submit(kind, s_draft)) s_edit = 0;
        } else if(key == KEY_LEFT || key == KEY_RIGHT) {
            uint8_t digits = 3;
            s_digit_index = (uint8_t)((s_digit_index + digits +
                             (key == KEY_RIGHT ? 1 : digits - 1)) % digits);
        } else if(key == KEY_UP || key == KEY_DOWN) {
            static const int32_t temp_steps[3] = {1000, 100, 10};
            static const int32_t time_steps[3] = {600, 60, 6};
            static const int32_t delay_steps[3] = {100, 10, 1};
            int32_t step, lo, hi;
            if(s_focus_index == 0) { step = temp_steps[s_digit_index]; lo = APP_TEMP_MIN_CENTI; hi = APP_TEMP_MAX_CENTI; }
            else if(s_focus_index == 1) { step = time_steps[s_digit_index]; lo = (int32_t)APP_TIME_MIN_S; hi = (int32_t)APP_TIME_MAX_S; }
            else { step = delay_steps[s_digit_index]; lo = 0; hi = APP_DELAY_MAX_S; }
            s_draft += key == KEY_UP ? step : -step;
            if(s_draft < lo) s_draft = lo;
            if(s_draft > hi) s_draft = hi;
        }
    } else if(key == KEY_OK) {
        if(s_focus_index < 3) {
            s_edit = 1;
            s_digit_index = 2;
            s_draft = s_focus_index == 0 ? s_view.run.target_centi :
                      s_focus_index == 1 ? (int32_t)s_view.run.time_s :
                                           s_view.run.delay_s;
            if(s_focus_index == 1 && s_draft < (int32_t)APP_TIME_MIN_S)
                s_draft = (int32_t)APP_TIME_MIN_S;
        } else if(s_focus_index == 3) {
            app_service_submit(APP_START, 0);
        } else if(s_focus_index == 4) {
            app_service_submit(APP_STOP, 0);
        } else if(s_focus_index == 5) {
            app_service_submit(s_view.door == MOTOR_OPEN ? APP_CLOSE : APP_OPEN, 0);
        } else {
            app_service_submit(APP_MUTE, 0);
        }
    } else if(key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN) {
        nav(key);
    }
    focus_update();
    s_refresh_ms = HAL_GetTick() - 100;
}

static int digit_x(const lv_font_t *font, const char *s, uint8_t index)
{
    int x = 8;
    uint8_t i;
    lv_font_glyph_dsc_t d;
    for(i = 0; i < index; ++i)
        if(lv_font_get_glyph_dsc(font, &d, s[i], s[i + 1])) x += d.adv_w;
    return x;
}

void ui_layout_process(void)
{
    char b[96], number[24];
    uint32_t now = HAL_GetTick(), width, total, remain;
    int32_t t, display_target;
    inc_phase_t ph;
    const char *phase_txt;
    uint32_t phase_col;
    int heat = 0, cool = 0;

    if(s_ok && s_edit && s_repeat_key < KEY_COUNT && key_is_pressed((key_id_t)s_repeat_key)) {
        if((uint32_t)(now - s_hold_ms) >= 500 && (uint32_t)(now - s_repeat_ms) >= 80) {
            s_repeating = 1;
            if(!s_edit) s_repeat_key = KEY_COUNT;
            else ui_layout_key(s_repeat_key);
            s_repeating = 0;
            s_repeat_ms = now;
        }
    } else s_repeat_key = KEY_COUNT;
    if(!s_ok || (uint32_t)(now - s_refresh_ms) < 100) return;
    s_refresh_ms = now;

    app_service_snapshot(&s_view);
    ph = s_view.run.phase;

    if(!allowed(s_focus_index)) {
        uint8_t i;
        s_edit = 0;
        for(i = 0; i < 7; ++i) if(allowed(i)) { s_focus_index = i; break; }
        focus_update();
    }

    /* Header: setpoint and temperature target. */
    display_target = s_view.temp.target_valid ? s_view.temp.applied_centi : s_view.run.target_centi;
    snprintf(b, sizeof(b), S_TARGET_FMT, (long)(display_target / 100),
             (long)((display_target % 100) / 10));
    value(s_target, b);
    snprintf(b, sizeof(b), S_SET_FMT, (long)(display_target / 100),
             (long)((display_target % 100) / 10));
    value(s_sv, b);

    /* Live temperature. */
    t = (s_edit && s_focus_index == 0) ? s_draft : s_view.temp.pv_centi;
    if(s_edit && s_focus_index == 0) {
        snprintf(number, sizeof(number), "%02ld.%ld", (long)(t / 100), (long)((t % 100) / 10));
    } else if(s_view.temp.valid) {
        int32_t tenth = t >= 0 ? (t + 5) / 10 : -((-t + 5) / 10);
        int32_t abs_t = tenth < 0 ? -tenth : tenth;
        snprintf(number, sizeof(number), "%s%ld.%ld", tenth < 0 ? "-" : "",
                 (long)(abs_t / 10), (long)(abs_t % 10));
    } else strcpy(number, "--.-");
    value(s_pv, number);
    tint(s_pv, !s_view.temp.valid ? RED :
         (ph == INC_COOL_REQUEST || ph == INC_COOLING) &&
         !s_view.at_temperature ? ORANGE : GREEN);
    tint(s_alarm, s_view.alarm ? RED : MUTED);
    width = 0;
    if((ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE) &&
       s_view.temp.valid && s_view.run.target_centi > 400) {
        int32_t done = s_view.run.target_centi - s_view.temp.pv_centi;
        int32_t span = s_view.run.target_centi - 400;
        if(done < 0) done = 0;
        if(done > span) done = span;
        width = (uint32_t)((int64_t)done * BAR_W / span);
    }
    bar(NULL, s_temp_bar, width, GREEN);

    /* Lid. */
    switch(s_view.door) {
    case MOTOR_OPEN: value(s_door, S_LID S_DOOR_OPEN); break;
    case MOTOR_CLOSED: value(s_door, S_LID S_DOOR_CLOSED); break;
    case MOTOR_OPENING: case MOTOR_CLOSING: case MOTOR_INSTALLING:
        value(s_door, S_LID S_DOOR_MOVING); break;
    case MOTOR_FAULT: value(s_door, S_LID S_DOOR_FAULT); break;
    default: value(s_door, S_LID "--"); break;
    }
    tint(s_door, s_view.door_fault ? RED : GREEN);

    /* Phase. */
    if(s_view.alarm) {
        phase_txt = (!s_view.temp.online || !s_view.temp.valid) ? S_COMM_ERROR :
                    (s_view.temp.alarm || s_view.write_fault) ? S_TEMP_ERROR : S_LID_ERROR;
        phase_col = RED;
    }
    else if(s_view.run.buzzer) { phase_txt = S_BUZZ; phase_col = ORANGE; }
    else switch(ph) {
    case INC_RUNNING:
        if(s_view.at_temperature) { phase_txt = S_REACH; phase_col = GREEN; }
        else if(s_view.temp.pv_centi < display_target) { phase_txt = S_HEAT; phase_col = RED; heat = 1; }
        else { phase_txt = S_COOL; phase_col = BLUE; cool = 1; }
        break;
    case INC_DELAY:
        phase_txt = s_view.at_temperature ? S_HOLD : S_HEAT;
        phase_col = s_view.at_temperature ? GREEN : RED;
        heat = s_view.at_temperature ? 0 : 1;
        break;
    case INC_COOL_REQUEST: case INC_COOLING:
        phase_txt = S_COOL; phase_col = BLUE; cool = 1; break;
    case INC_STORAGE:
        phase_txt = S_SAVE; phase_col = GREEN; break;
    case INC_FINISHED:
        phase_txt = S_DONE; phase_col = MUTED; break;
    case INC_ERROR:
        phase_txt = "ERROR"; phase_col = RED; break;
    default:
        if(!s_view.temp.valid) { phase_txt = S_WAIT; phase_col = MUTED; }
        else if(s_view.at_temperature) { phase_txt = S_HOLD; phase_col = GREEN; }
        else if(s_view.temp.pv_centi < display_target) { phase_txt = S_HEAT; phase_col = RED; heat = 1; }
        else { phase_txt = S_COOL; phase_col = BLUE; cool = 1; }
        break;
    }
    value(s_phase, phase_txt);
    tint(s_phase, phase_col);
    icon_state(heat, cool, phase_col);

    /* Incubation time. */
    total = s_view.run.time_s;
    remain = (s_view.run.remaining_ms + 999u) / 1000u;
    if(s_edit && s_focus_index == 1) {
        snprintf(number, sizeof(number), "%02lu.%lu min",
                 (unsigned long)(s_draft / 60), (unsigned long)((s_draft % 60) / 6));
    } else if(s_view.alarm) {
        strcpy(number, "--:--");
    } else if(ph == INC_RUNNING) {
        clock_text(number, sizeof(number), remain);
    } else if(ph == INC_IDLE) {
        snprintf(number, sizeof(number), "%02lu.%lu min",
                 (unsigned long)(total / 60), (unsigned long)((total % 60) / 6));
    } else strcpy(number, "00:00");
    value(s_time, number);
    tint(s_time, s_view.alarm ? RED : ph == INC_RUNNING ? ORANGE : 0xD2ECDD);

    if(ph == INC_RUNNING) {
        if(s_view.run.paused) value(s_time_aux, S_PAUSED);
        else if(total < 3600u)
            snprintf(b, sizeof(b), S_REMAIN_MIN, (unsigned long)(total / 60u),
                     (unsigned long)((total % 60u) / 6u));
        else
            snprintf(b, sizeof(b), S_REMAIN_H, (unsigned long)(total / 3600u),
                     (unsigned long)((total % 3600u) / 360u));
        if(!s_view.run.paused) value(s_time_aux, b);
    } else if(ph == INC_IDLE) {
        value(s_time_aux, S_WAIT);
    } else {
        value(s_time_aux, s_view.run.paused ? S_PAUSED : S_DONE);
    }

    width = 0;
    if(ph == INC_RUNNING && s_view.run.run_duration_ms)
        width = (uint32_t)((uint64_t)(s_view.run.run_duration_ms - s_view.run.remaining_ms) *
                           BAR_W / s_view.run.run_duration_ms);
    else if(ph > INC_RUNNING) width = BAR_W;
    bar(NULL, s_time_bar, width, GREEN);

    /* Automatic-cooling delay. */
    if(s_edit && s_focus_index == 2) {
        snprintf(number, sizeof(number), "%03lu", (unsigned long)s_draft);
    } else if(ph == INC_DELAY) {
        snprintf(number, sizeof(number), "%lu",
                 (unsigned long)((s_view.run.delay_remaining_ms + 999u) / 1000u));
    } else if(ph == INC_COOL_REQUEST || ph == INC_COOLING) {
        strcpy(number, "0");
    } else if(ph == INC_STORAGE) {
        strcpy(number, "--");
    } else if(s_view.run.delay_s) {
        snprintf(number, sizeof(number), "%lu", (unsigned long)s_view.run.delay_s);
    } else {
        strcpy(number, "N");   /* 0 s: no automatic cooling */
    }
    value(s_delay, number);

    if(s_edit && s_focus_index == 2)
        snprintf(b, sizeof(b), "%lu s", (unsigned long)s_draft);
    else if(ph == INC_DELAY && s_view.run.delay_s)
        snprintf(b, sizeof(b), "%lu/%u",
                 (unsigned long)((s_view.run.delay_remaining_ms + 999u) / 1000u),
                 s_view.run.delay_s);
    else if(s_view.run.delay_s) snprintf(b, sizeof(b), "%u s", s_view.run.delay_s);
    else strcpy(b, "N");
    value(s_delay_set, b);

    if(s_view.message != APP_MSG_NONE) {
        const char *msg = s_view.message == APP_MSG_BUSY ? "BUSY" :
                          s_view.message == APP_MSG_SET_TEMP ? S_SET_TEMP_MSG :
                          s_view.message == APP_MSG_CLOSE_LID ? S_CLOSE :
                          s_view.message == APP_MSG_TEMP_OFFLINE ? S_COMM_ERROR :
                          s_view.message == APP_MSG_DOOR_FAILED ? S_LID_ERROR :
                          s_view.message == APP_MSG_SETTING_LOCKED ? S_STARTING : S_TEMP_ERROR;
        value(s_delay_state, msg);
        tint(s_delay_state, RED);
    } else if(s_edit && s_focus_index == 2) {
        value(s_delay_state, S_EDITABLE);
        tint(s_delay_state, YELLOW);
    } else if(ph == INC_DELAY) {
        snprintf(b, sizeof(b), S_AFTER, (unsigned long)((s_view.run.delay_remaining_ms + 999u) / 1000u));
        value(s_delay_state, b);
        tint(s_delay_state, ORANGE);
    } else if(ph == INC_COOL_REQUEST || ph == INC_COOLING) {
        value(s_delay_state, S_COOLING);
        tint(s_delay_state, BLUE);
    } else if(ph == INC_STORAGE) {
        value(s_delay_state, S_KEEP);
        tint(s_delay_state, GREEN);
    } else if(ph == INC_RUNNING) {
        value(s_delay_state, S_EDITABLE);
        tint(s_delay_state, MUTED);
    } else if(ph == INC_IDLE) {
        value(s_delay_state, S_EDITABLE);
        tint(s_delay_state, MUTED);
    } else {
        value(s_delay_state, S_DONE);
        tint(s_delay_state, MUTED);
    }

    width = 0;
    if(ph == INC_DELAY && s_view.run.run_delay_ms)
        width = (uint32_t)((uint64_t)(s_view.run.run_delay_ms - s_view.run.delay_remaining_ms) *
                           BAR_W / s_view.run.run_delay_ms);
    else if(ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE)
        width = BAR_W;
    bar(NULL, s_delay_bar, width,
        (ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE) ? GREEN : YELLOW);

    /* Action keys. */
    if(ph == INC_RUNNING || ph == INC_DELAY) {
        value(s_kt[0], S_STARTING); tint(s_kt[0], ORANGE);
        value(s_kn[0], ""); tint(s_kn[0], MUTED);
    } else if(ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE) {
        value(s_kt[0], S_START); tint(s_kt[0], DISABLED);
        value(s_kn[0], S_DONE); tint(s_kn[0], DISABLED);
    } else if(ph == INC_IDLE) {
        value(s_kt[0], S_START); tint(s_kt[0], GREEN);
        value(s_kn[0], "SET"); tint(s_kn[0], MUTED);
    } else {
        value(s_kt[0], S_START); tint(s_kt[0], DISABLED);
        value(s_kn[0], S_DONE); tint(s_kn[0], DISABLED);
    }

    if(incubator_model_active(&s_view.run) || ph == INC_FINISHED || ph == INC_ERROR) {
        value(s_kt[1], S_STOP); tint(s_kt[1], GREEN);
        value(s_kn[1], "SET"); tint(s_kn[1], MUTED);
    } else {
        value(s_kt[1], S_STOP); tint(s_kt[1], DISABLED);
        value(s_kn[1], "--"); tint(s_kn[1], DISABLED);
    }

    if(s_view.door == MOTOR_OPEN) {
        value(s_kt[2], S_CLOSE); tint(s_kt[2], GREEN);
        value(s_kn[2], S_DOOR_OPEN); tint(s_kn[2], MUTED);
    } else if(s_view.door == MOTOR_CLOSED) {
        value(s_kt[2], S_OPEN); tint(s_kt[2], GREEN);
        value(s_kn[2], S_DOOR_CLOSED); tint(s_kn[2], MUTED);
    } else if(s_view.door == MOTOR_OPENING || s_view.door == MOTOR_CLOSING ||
              s_view.door == MOTOR_INSTALLING) {
        value(s_kt[2], S_DOOR_MOVING); tint(s_kt[2], MUTED);
        value(s_kn[2], "--"); tint(s_kn[2], MUTED);
    } else if(s_view.door == MOTOR_FAULT) {
        value(s_kt[2], S_DOOR_FAULT); tint(s_kt[2], RED);
        value(s_kn[2], "SET"); tint(s_kn[2], MUTED);
    } else {
        value(s_kt[2], S_OPEN); tint(s_kt[2], GREEN);
        value(s_kn[2], "--"); tint(s_kn[2], MUTED);
    }

    if(s_view.run.buzzer) {
        value(s_kt[3], S_MUTE); tint(s_kt[3], RED);
        value(s_kn[3], "SET"); tint(s_kn[3], MUTED);
    } else {
        value(s_kt[3], S_MUTE); tint(s_kt[3], DISABLED);
        value(s_kn[3], S_NOT_BUZZ); tint(s_kn[3], DISABLED);
    }

    key_icons(ph == INC_RUNNING || ph == INC_DELAY ? ORANGE :
              (ph == INC_IDLE ? GREEN : DISABLED),
              (incubator_model_active(&s_view.run) || ph == INC_FINISHED || ph == INC_ERROR) ? GREEN : DISABLED,
              s_view.door == MOTOR_FAULT ? RED : GREEN,
              s_view.run.buzzer ? RED : DISABLED);

    /* Editing cursor. */
    if(s_edit) {
        char cursor[24];
        const lv_font_t *cf = F28;
        uint8_t ci = s_digit_index;   /* index of the glyph inside cursor[] */
        int x, y;
        lv_font_glyph_dsc_t d;
        if(s_focus_index == 0) {
            snprintf(cursor, sizeof(cursor), "%02ld.%ld", (long)(s_draft / 100), (long)((s_draft % 100) / 10));
            if(ci == 2) ++ci;       /* decimal point is not an editable digit */
            y = 70;
        } else if(s_focus_index == 1) {
            snprintf(cursor, sizeof(cursor), "%02lu.%lu", (unsigned long)(s_draft / 60),
                     (unsigned long)((s_draft % 60) / 6));
            if(ci == 2) ++ci;
            y = 132;
        } else {
            snprintf(cursor, sizeof(cursor), "%03lu", (unsigned long)s_draft);
            cf = F28;
            y = 204;
        }
        x = digit_x(cf, cursor, ci);
        if(lv_font_get_glyph_dsc(cf, &d, cursor[ci], cursor[ci + 1]))
            lv_obj_set_width(s_digit, d.adv_w);
        lv_obj_set_pos(s_digit, x, y);
        lv_obj_remove_flag(s_digit, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_digit, LV_OBJ_FLAG_HIDDEN);

    if(!s_focus_shown) focus_update();
}
