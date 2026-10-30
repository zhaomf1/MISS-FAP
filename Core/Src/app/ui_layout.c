#include "ui.h"
#include "key.h"
#include "app_service.h"
#include "lv_font_ui_font.h"
#include "ui_strings_generated.h"
#include <stdio.h>
#include <string.h>

/*
 * 主界面布局模块：负责温度、目标值、运行状态、舱门状态和按键焦点显示。
 * 本模块只提交 app_service 命令，不直接执行温控器或电机操作。
 */

/*
 * 界面文案以 UTF-8 字节转义写入 C 字符串，兼容 ARMCC 5 源码编码处理。
 * 这样可避开 Keil ARMCC 5 在不同 Windows 代码页下解析中文源码字符串不一致的问题。
 */
/* UI文案从 ui_strings_zh.txt 生成，修改文案后请重新生成头文件。 */
/* 保持字库位图数据为只读；小型描述符副本为字库子集缺少的空格和标点提供拉丁字母回退。 */
static lv_font_t s_font12, s_font14, s_font28, s_font32;
#define F12 (&s_font12)
#define F14 (&s_font14)
#define F28 (&s_font28)
#define F32 (&s_font32)

/* 颜色格式为 RGB888（0xRRGGBB）；调高各通道数值会使颜色更亮。 */
#define BG        0x193323 /* 页面主背景及左下操作卡片底色 */
#define HEADER    0x142B1C /* 顶部状态栏背景 */
#define PANEL     0x20452F /* 右上开始操作卡片背景 */
#define PANEL_ALT 0x1C3D29 /* 右侧其余操作卡片背景 */
#define LINE      0x3B684C /* 区域分隔线和卡片边框 */
#define GREEN     0x6FFFA7 /* 正常、选中及进度状态 */
#define MUTED     0x739C88 /* 普通说明文字和弱化状态 */
#define DISABLED  0x446251 /* 当前不可操作控件 */
#define YELLOW    0xFFD54A /* 参数编辑焦点和延迟提示 */
#define RED       0xFF7770 /* 报警或无效温度 */
#define ORANGE    0xFFA54A /* 运行及降温过渡状态 */
#define BLUE      0x5FC8FF /* 降温状态 */

/* 屏幕分区：顶部栏 24 像素，温度/时间各 64 像素，延迟区 88 像素。 */
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240
#define HEADER_H      24
#define LEFT_W        200
#define ACTION_LEFT_X 201
#define ACTION_RIGHT_X 260
#define ACTION_LEFT_W 59
#define ACTION_RIGHT_W 60
#define ACTION_TOP_Y  24
#define ACTION_ROW_H  72
#define ACTION_LOW_Y  (ACTION_TOP_Y + ACTION_ROW_H)
#define ACTION_TITLE_OFFSET 30
#define ACTION_NOTE_OFFSET  52
#define ACTION_ICON_SIZE 10
#define ICON_PLAY_X   226
#define ICON_PLAY_Y   32
#define ICON_STOP_X   285
#define ICON_STOP_Y   32
#define ICON_DOOR_X   226
#define ICON_DOOR_Y   104
#define ICON_MUTE_X   285
#define ICON_MUTE_Y   104
#define TEMP_Y   24
#define LEFT_ROW_H ACTION_ROW_H
#define TEMP_H   LEFT_ROW_H
#define TIME_Y   (TEMP_Y + LEFT_ROW_H)
#define TIME_H   LEFT_ROW_H
#define DELAY_Y  (TIME_Y + LEFT_ROW_H)
#define DELAY_H  LEFT_ROW_H
#define BAR_W    184
#define BAR_H    4

/* 焦点编号与屏幕控件一一对应，输入导航和状态刷新共用此定义。 */
typedef enum {
    FOCUS_TEMPERATURE = 0,
    FOCUS_TIME,
    FOCUS_DELAY,
    FOCUS_START,
    FOCUS_STOP,
    FOCUS_DOOR,
    FOCUS_MUTE,
    FOCUS_COUNT
} ui_focus_t;

/* 操作卡片数组的下标与右侧控件排列一致：开始、停止、舱门、消音。 */
typedef enum {
    ACTION_START = 0,
    ACTION_STOP,
    ACTION_DOOR,
    ACTION_MUTE,
    ACTION_COUNT
} ui_action_t;

/* UI 只维护控件句柄和编辑状态，不在界面线程执行阻塞串口事务。 */
static lv_obj_t *s_time_title;
static lv_obj_t *s_pv, *s_target, *s_time, *s_time_aux, *s_delay;
static lv_obj_t *s_temp_unit;
static lv_obj_t *s_delay_unit;
static lv_obj_t *s_delay_set, *s_delay_state;
/* 三栏进度条分别显示温度、孵育时间和延迟降温阶段进度。 */
static lv_obj_t *s_temp_bar_track, *s_temp_bar_fill;
static lv_obj_t *s_time_bar_track, *s_time_bar_fill;
static lv_obj_t *s_delay_bar_track, *s_delay_bar_fill;
static lv_obj_t *s_alarm, *s_door, *s_phase, *s_sv;
static lv_obj_t *s_kt[ACTION_COUNT], *s_kn[ACTION_COUNT];       /* 四个操作卡片的主文字和辅助文字 */
static lv_obj_t *s_ic_start, *s_ic_clock, *s_ic_stop, *s_ic_door, *s_ic_mute;
static lv_obj_t *s_ic_up[3], *s_ic_down[3];
static lv_obj_t *s_focus, *s_digit;
static uint8_t s_ok, s_focus_index, s_edit, s_digit_index;
static int32_t s_draft;
/* 默认参数不算用户已设置；用户确认设置后，待机时也显示对应进度轨道。 */
static uint8_t s_time_configured, s_delay_configured;
/* 确认温度设定时的温度和目标值，用于显示本次调温进度。 */
static int32_t s_temp_progress_start_centi, s_temp_progress_target_centi;
static uint8_t s_temp_progress_valid, s_temp_progress_waiting;
static app_snapshot_t s_view;
static uint32_t s_refresh_ms;
static uint32_t s_repeat_key = KEY_COUNT, s_hold_ms, s_repeat_ms;
static uint8_t s_repeating;

/* 将按键和当前焦点转换为稳定的英文日志名，避免串口终端中文编码不一致。 */
static const char *ui_key_name(uint32_t key)
{
    switch(key) {
    case KEY_LEFT: return "LEFT";
    case KEY_RIGHT: return "RIGHT";
    case KEY_OK: return "OK";
    case KEY_UP: return "UP";
    case KEY_DOWN: return "DOWN";
    default: return "UNKNOWN";
    }
}

static const char *ui_focus_name(uint8_t focus)
{
    static const char *const names[FOCUS_COUNT] = {
        "TEMPERATURE", "INCUBATION_TIME", "COOL_DELAY", "START",
        "STOP", "DOOR", "MUTE"
    };
    return focus < FOCUS_COUNT ? names[focus] : "UNKNOWN";
}

/* 创建一个无滚动、无点击的矩形 LVGL 对象，用作背景、分隔线或进度条。 */
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

/* 创建一个固定位置和尺寸的文本标签。 */
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

/* 显示内容没有变化时不重复分配或触发 LVGL 刷新。 */
/* 只有文字发生变化时才更新标签，减少刷新开销。 */
static void value(lv_obj_t *o, const char *v)
{
    if(o && strcmp(lv_label_get_text(o), v) != 0) lv_label_set_text(o, v);
}

/* 修改文本或图标的前景颜色。 */
static void tint(lv_obj_t *o, uint32_t c)
{
    if(o) lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
}

/* 更新进度条的显示状态和填充宽度；未配置且未运行时轨道与填充均隐藏。 */
static void bar(lv_obj_t *track, lv_obj_t *fill, int visible,
                uint32_t width, uint32_t color)
{
    if(!track || !fill) return;
    if(!visible) {
        lv_obj_add_flag(track, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(track, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(fill, (int32_t)width);
    lv_obj_set_style_bg_color(fill, lv_color_hex(color), 0);
    if(!width) {
        lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(fill, LV_OBJ_FLAG_HIDDEN);
}

/* 小于一小时显示为 12:36，大于等于一小时显示为 2:05:00。 */
/* 将秒数格式化为 MM:SS 或 H:MM:SS。 */
static void clock_text(char *b, size_t n, uint32_t sec)
{
    uint32_t h = sec / 3600u, m = (sec / 60u) % 60u;
    if(h) snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)h,
                   (unsigned long)m, (unsigned long)(sec % 60u));
    else snprintf(b, n, "%02lu:%02lu", (unsigned long)m,
                  (unsigned long)(sec % 60u));
}

/* 判断当前状态下某个焦点是否允许操作。 */
static int allowed(uint8_t i)
{
    inc_phase_t ph = s_view.run.phase;
    switch(i) {
    case FOCUS_TEMPERATURE: return ph == INC_IDLE;
    case FOCUS_TIME: return ph == INC_IDLE;
    case FOCUS_DELAY: return ph == INC_IDLE || ph == INC_RUNNING;
    case FOCUS_START: return ph == INC_IDLE && s_view.temp.online &&
                   s_view.temp.valid && !s_view.temp.alarm && s_view.temp.target_valid;
    case FOCUS_STOP: return incubator_model_active(&s_view.run) ||
                   ph == INC_FINISHED || ph == INC_ERROR;           /* 停止：运行中、已完成或出错时可操作 */
    case FOCUS_DOOR: return s_view.door != MOTOR_OPENING &&
                   s_view.door != MOTOR_CLOSING &&
                   s_view.door != MOTOR_INSTALLING;                 /* 舱门：电机动作期间禁止重复触发 */
    case FOCUS_MUTE: return s_view.run.buzzer != 0;
    }
    return 0;
}

/* 四个操作图标始终显示；焦点框和图标颜色共同指示当前选择项。 */
static void action_icon_visibility(void)
{
    int timing = s_view.run.phase == INC_RUNNING || s_view.run.phase == INC_DELAY;
    if(timing) {
        lv_obj_add_flag(s_ic_start, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_ic_clock, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_ic_start, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ic_clock, LV_OBJ_FLAG_HIDDEN);
    }

}

/* 根据具名焦点编号移动焦点框，并同步刷新操作图标显隐。 */
static void focus_update(void)
{
    static const int bounds[FOCUS_COUNT][4] = {
        {2, TEMP_Y + 2, 196, TEMP_H - 4},
        {2, TIME_Y + 2, 196, TIME_H - 4},
        {2, DELAY_Y + 2, 196, DELAY_H - 4},
        {ACTION_LEFT_X + 1, ACTION_TOP_Y + 1, ACTION_LEFT_W - 2, ACTION_ROW_H - 2},
        {ACTION_RIGHT_X + 1, ACTION_TOP_Y + 1, ACTION_RIGHT_W - 2, ACTION_ROW_H - 2},
        {ACTION_LEFT_X + 1, ACTION_LOW_Y + 1, ACTION_LEFT_W - 2, ACTION_ROW_H - 2},
        {ACTION_RIGHT_X + 1, ACTION_LOW_Y + 1, ACTION_RIGHT_W - 2, ACTION_ROW_H - 2}
    };
    const int *b = bounds[s_focus_index];
    lv_obj_set_pos(s_focus, b[0], b[1]);
    lv_obj_set_size(s_focus, b[2], b[3]);
    lv_obj_set_style_border_color(s_focus, lv_color_hex(s_edit ? YELLOW : GREEN), 0);
    action_icon_visibility();
}

/* 显示升温、降温或恒温状态对应的顶部图标。 */
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

}

/* 设置操作图标的颜色。 */
static void key_icon_color(lv_obj_t *o, uint32_t c)
{
    if(o) {
        lv_obj_set_style_text_color(o, lv_color_hex(c), 0);
        lv_obj_invalidate(o);
    }
}

/* 统一更新开始、停止、舱门和消音图标的状态颜色。 */
static void key_icons(uint32_t start_c, uint32_t stop_c,
                      uint32_t door_c, uint32_t mute_c)
{
    key_icon_color(s_ic_start, start_c);
    key_icon_color(s_ic_clock, start_c);
    key_icon_color(s_ic_stop, stop_c);
    key_icon_color(s_ic_door, door_c);
    key_icon_color(s_ic_mute, mute_c);
}

/* 创建左侧信息区分隔线和右侧操作卡片。 */
static void create_page_frame(lv_obj_t *screen)
{
    rect(screen, 0, 0, SCREEN_WIDTH, HEADER_H, HEADER, 0);
    rect(screen, 0, HEADER_H - 1, SCREEN_WIDTH, 1, LINE, 0);
    rect(screen, LEFT_W, HEADER_H, 1, SCREEN_HEIGHT - HEADER_H, LINE, 0);
    /* 温度栏与孵育栏之间的分隔线，位于两栏交界处。 */
    rect(screen, 0, TIME_Y - 1, LEFT_W, 1, LINE, 0);
    rect(screen, 0, DELAY_Y - 1, LEFT_W, 1, LINE, 0);

    /* 三个进度条初始隐藏，配置完成或进入运行流程后再显示。 */
    /* 进度条上移并加粗，和各栏分隔线/屏幕边缘留出间距。 */
    s_temp_bar_track = rect(screen, 8, TEMP_Y + TEMP_H - 8, BAR_W, BAR_H, 0x34513E, 0);
    s_temp_bar_fill = rect(screen, 8, TEMP_Y + TEMP_H - 8, 0, BAR_H, GREEN, 0);
    s_time_bar_track = rect(screen, 8, TIME_Y + TIME_H - 8, BAR_W, BAR_H, 0x34513E, 0);
    s_time_bar_fill = rect(screen, 8, TIME_Y + TIME_H - 8, 0, BAR_H, ORANGE, 0);
    s_delay_bar_track = rect(screen, 8, DELAY_Y + DELAY_H - 8, BAR_W, BAR_H, 0x34513E, 0);
    s_delay_bar_fill = rect(screen, 8, DELAY_Y + DELAY_H - 8, 0, BAR_H, BLUE, 0);
    bar(s_temp_bar_track, s_temp_bar_fill, 0, 0, GREEN);
    bar(s_time_bar_track, s_time_bar_fill, 0, 0, ORANGE);
    bar(s_delay_bar_track, s_delay_bar_fill, 0, 0, BLUE);

    /* 右侧四个操作卡片按 2 列 2 行排列。 */
    rect(screen, ACTION_LEFT_X, ACTION_TOP_Y, ACTION_LEFT_W, ACTION_ROW_H, PANEL, 1);
    rect(screen, ACTION_RIGHT_X, ACTION_TOP_Y, ACTION_RIGHT_W, ACTION_ROW_H, PANEL_ALT, 1);
    rect(screen, ACTION_LEFT_X, ACTION_LOW_Y, ACTION_LEFT_W, ACTION_ROW_H, BG, 1);
    rect(screen, ACTION_RIGHT_X, ACTION_LOW_Y, ACTION_RIGHT_W, ACTION_ROW_H, PANEL_ALT, 1);

}

/* 创建顶部状态栏：报警、舱门、运行阶段和当前设定温度。 */
static void create_header_area(lv_obj_t *screen)
{
    s_alarm = text(screen, "ALM", 6, 4, 32, 16, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_door = text(screen, S_LID S_DOOR_CLOSED, 42, 4, 62, 16, F12, GREEN, LV_TEXT_ALIGN_LEFT);
    s_phase = text(screen, S_WAIT, 116, 4, 70, 16, F12, MUTED, LV_TEXT_ALIGN_CENTER);
    s_sv = text(screen, "", 188, 4, 126, 16, F12, MUTED, LV_TEXT_ALIGN_RIGHT);
}

/* 创建实时温度和目标温度显示区域。 */
static void create_temperature_area(lv_obj_t *screen)
{
    text(screen, S_TEMP_TITLE, 8, TEMP_Y + 10, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_pv = text(screen, "--.-", 8, TEMP_Y + 29, 112, 32, F28, GREEN, LV_TEXT_ALIGN_LEFT);
    s_temp_unit = text(screen, "\xE2\x84\x83", 0, TEMP_Y + 29, 36, 32, F28, MUTED, LV_TEXT_ALIGN_LEFT);
    /* 当前/目标提示靠右下角显示，避免占用温度主数值区域。 */
    s_target = text(screen, "", 8, TEMP_Y + 47, 184, 12, F12, MUTED, LV_TEXT_ALIGN_RIGHT);
}

/* 创建孵育时间主数值和剩余时间/运行状态说明。 */
static void create_timing_area(lv_obj_t *screen)
{
    s_time_title = text(screen, S_TIME_TITLE, 8, TIME_Y + 10, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_time = text(screen, "15.0 min", 8, TIME_Y + 29, 184, 27, F28, 0xD2ECDD, LV_TEXT_ALIGN_LEFT);
    /* 等待/暂停/剩余状态固定放在孵育栏右下角，与温度目标提示对齐。 */
    s_time_aux = text(screen, S_WAIT, 8, TIME_Y + 46, 184, 12, F12, MUTED, LV_TEXT_ALIGN_RIGHT);
}

/* 创建自动降温延迟设置、倒计时和状态提示区域。 */
static void create_delay_area(lv_obj_t *screen)
{
    text(screen, S_DELAY_TITLE, 8, DELAY_Y + 12, 124, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_delay_set = text(screen, "", 140, DELAY_Y + 12, 52, 14, F12, MUTED, LV_TEXT_ALIGN_RIGHT);
    s_delay = text(screen, "0", 8, DELAY_Y + 30, 60, 32, F28, YELLOW, LV_TEXT_ALIGN_LEFT);
    s_delay_unit = text(screen, "s", 0, DELAY_Y + 30, 32, 32, F28, MUTED, LV_TEXT_ALIGN_LEFT);
    /* 延迟阶段的小字状态靠右显示，避免与左侧大号秒数重叠。 */
    s_delay_state = text(screen, "", 92, DELAY_Y + 46, 100, 12, F12, MUTED, LV_TEXT_ALIGN_RIGHT);
}

/* 单对象图标绘制：直接绘制像素段，不为每行像素分配 LVGL 对象。
 * 避免图标对象过多耗尽 LVGL 堆，导致页面初始化进入故障红屏。 */
static void draw_action_symbol(lv_event_t *event)
{
    const char *const *rows = (const char *const *)lv_event_get_user_data(event);
    lv_obj_t *obj = (lv_obj_t *)lv_event_get_target(event);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_area_t bounds, segment;
    lv_draw_rect_dsc_t dsc;
    int row, col;
    lv_obj_get_coords(obj, &bounds);
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_obj_get_style_text_color(obj, 0);
    dsc.bg_opa = LV_OPA_COVER;
    for(row = 0; row < 14; ++row) {
        col = 0;
        while(col < 14) {
            int start;
            if(rows[row][col] != '#') { ++col; continue; }
            start = col;
            while(col < 14 && rows[row][col] == '#') ++col;
            segment.x1 = bounds.x1 + start;
            segment.x2 = bounds.x1 + col - 1;
            segment.y1 = segment.y2 = bounds.y1 + row;
            lv_draw_rect(layer, &dsc, &segment);
        }
    }
}

/* 模板使用静态存储，绘制回调在页面整个生命周期内均可读取。 */
static lv_obj_t *create_action_symbol(lv_obj_t *screen, int x, int y,
                                      const char *const rows[14])
{
    lv_obj_t *obj = rect(screen, x, y, 14, 14, BG, 0);
    if(!obj) return NULL;
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(GREEN), 0);
    lv_obj_add_event_cb(obj, draw_action_symbol, LV_EVENT_DRAW_MAIN, (void *)rows);
    return obj;
}

/* 创建阶段图标和右侧操作图标；四个操作图标各自位于对应卡片内。 */
static void create_icons(lv_obj_t *screen)
{
    /* 升温/降温用三层像素箭头，恒温时显示中心圆点。 */
    s_ic_up[0] = rect(screen, 107, 7, 2, 2, GREEN, 0);
    s_ic_up[1] = rect(screen, 105, 10, 6, 2, GREEN, 0);
    s_ic_up[2] = rect(screen, 104, 13, 8, 2, GREEN, 0);
    s_ic_down[0] = rect(screen, 104, 7, 8, 2, BLUE, 0);
    s_ic_down[1] = rect(screen, 105, 10, 6, 2, BLUE, 0);
    s_ic_down[2] = rect(screen, 107, 13, 2, 2, BLUE, 0);

    /* 开始、计时、停止、关盖和消音模板；所有图元由各自容器统一管理。 */
    static const char *const play_symbol[14] = {
        "..............",
        "..##..........",
        "..####........",
        "..######......",
        "..########....",
        "..##########..",
        "..###########.",
        "..##########..",
        "..########....",
        "..######......",
        "..####........",
        "..##..........",
        "..............",
        ".............."
    };
    static const char *const clock_symbol[14] = {
        ".....####.....",
        "......##......",
        "....######....",
        "..##......##..",
        ".##...##...##.",
        ".#....##....#.",
        "##....##....##",
        "##....####..##",
        "##..........##",
        ".#..........#.",
        ".##........##.",
        "..##......##..",
        "....######....",
        ".............."
    };
    static const char *const stop_symbol[14] = {
        "..............",
        "..............",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..............",
        ".............."
    };
    static const char *const door_symbol[13] = {
        "......##......",
        ".....####.....",
        "....######....",
        "...########...",
        "..##########..",
        "..............",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..##########..",
        "..............",
        ".............."
    };
    static const char *const mute_symbol[14] = {
        "..............",
        ".#.....##.....",
        ".##...###.....",
        "..##.####.....",
        "...##.###.....",
        ".###.####.....",
        ".####.###.....",
        ".#####.##.....",
        ".######.#.....",
        ".....###.##...",
        "......##..##..",
        ".......#...##.",
        "............#.",
        ".............."
    };
    s_ic_start = create_action_symbol(screen, ICON_PLAY_X - 1, ICON_PLAY_Y, play_symbol);
    s_ic_clock = create_action_symbol(screen, ICON_PLAY_X - 1, ICON_PLAY_Y, clock_symbol);
    s_ic_stop = create_action_symbol(screen, ICON_STOP_X - 1, ICON_STOP_Y, stop_symbol);
    s_ic_door = create_action_symbol(screen, ICON_DOOR_X - 1, ICON_DOOR_Y, door_symbol);
    s_ic_mute = create_action_symbol(screen, ICON_MUTE_X - 1, ICON_MUTE_Y, mute_symbol);
}

/* 创建右侧操作卡片标签；图标和文字各占卡片内独立区域。 */
static void create_action_area(lv_obj_t *screen)
{
    static const int x[ACTION_COUNT] = {ACTION_LEFT_X, ACTION_RIGHT_X, ACTION_LEFT_X, ACTION_RIGHT_X};
    static const int y[ACTION_COUNT] = {ACTION_TOP_Y, ACTION_TOP_Y, ACTION_LOW_Y, ACTION_LOW_Y};
    static const int w[ACTION_COUNT] = {ACTION_LEFT_W, ACTION_RIGHT_W, ACTION_LEFT_W, ACTION_RIGHT_W};
    uint8_t i;
    for(i = 0; i < ACTION_COUNT; ++i) {
        s_kt[i] = text(screen, "", x[i], y[i] + ACTION_TITLE_OFFSET, w[i], 20,
                       F14, MUTED, LV_TEXT_ALIGN_CENTER);
        s_kn[i] = text(screen, "", x[i], y[i] + ACTION_NOTE_OFFSET, w[i], 14,
                       F12, MUTED, LV_TEXT_ALIGN_CENTER);
    }
}

/* 初始化页面级样式、字体和各区域控件；保持公共初始化入口不变。 */
void ui_layout_init(void)
{
    lv_obj_t *screen = lv_screen_active();
    s_font12 = lv_font_ui_12; s_font12.fallback = &lv_font_montserrat_14;
    s_font14 = lv_font_ui_14; s_font14.fallback = &lv_font_montserrat_14;
    s_font28 = lv_font_ui_28; s_font28.fallback = &lv_font_montserrat_20;
    s_font32 = lv_font_ui_32; s_font32.fallback = &lv_font_montserrat_20;
    s_ok = 1;
    s_focus_index = FOCUS_TEMPERATURE;
    s_edit = 0; s_digit_index = 0;
    s_time_configured = s_delay_configured = 0;
    s_temp_progress_valid = s_temp_progress_waiting = 0;

    lv_obj_clean(screen);
    lv_obj_remove_style_all(screen);
    lv_obj_set_size(screen, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_style_bg_color(screen, lv_color_hex(BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* 先铺背景和分隔结构，再放置图标、焦点框及文本，避免遮挡。 */
    create_page_frame(screen);
    create_icons(screen);
    s_focus = rect(screen, 2, TEMP_Y + 2, 196, TEMP_H - 4, BG, 1);
    if(!s_ok) return;
    lv_obj_set_style_bg_opa(s_focus, LV_OPA_TRANSP, 0);
    s_digit = rect(screen, 8, 72, 12, 2, YELLOW, 0);
    if(!s_ok) return;
    lv_obj_add_flag(s_digit, LV_OBJ_FLAG_HIDDEN);

    create_header_area(screen);
    create_temperature_area(screen);
    create_timing_area(screen);
    create_delay_area(screen);
    create_action_area(screen);
    if(!s_ok) return;

    focus_update();
    s_refresh_ms = HAL_GetTick() - 100;
    ui_layout_process();
}

/* 返回页面对象是否全部创建成功。 */
int ui_layout_ready(void) { return s_ok; }

/* 按方向映射移动焦点；不可操作的可见控件也保留稳定的焦点位置。 */
static void nav(uint32_t key)
{
    /* 几何排列：温度/开始/停止；时间/舱门/消音；延迟/空/空。
     * 每次仅移动一个相邻格，边缘及空位保持原焦点，禁止绕行与跨行。 */
    static const uint8_t right[FOCUS_COUNT] = {
        FOCUS_START, FOCUS_DOOR, FOCUS_DELAY, FOCUS_STOP,
        FOCUS_STOP, FOCUS_MUTE, FOCUS_MUTE
    };
    static const uint8_t left[FOCUS_COUNT] = {
        FOCUS_TEMPERATURE, FOCUS_TIME, FOCUS_DELAY, FOCUS_TEMPERATURE,
        FOCUS_START, FOCUS_TIME, FOCUS_DOOR
    };
    static const uint8_t up[FOCUS_COUNT] = {
        FOCUS_TEMPERATURE, FOCUS_TEMPERATURE, FOCUS_TIME, FOCUS_START,
        FOCUS_STOP, FOCUS_START, FOCUS_STOP
    };
    static const uint8_t down[FOCUS_COUNT] = {
        FOCUS_TIME, FOCUS_DELAY, FOCUS_DELAY, FOCUS_DOOR,
        FOCUS_MUTE, FOCUS_DOOR, FOCUS_MUTE
    };
    const uint8_t *map = key == KEY_RIGHT ? right : key == KEY_LEFT ? left :
                         key == KEY_UP ? up : down;
    s_focus_index = map[s_focus_index];
    s_edit = 0;
    focus_update();
}

/* 在当前数字草稿中移动编辑位，或按字段步长增减并限制上下界。 */
static void edit_numeric_digits(uint32_t key, const int32_t steps[3],
                                int32_t minimum, int32_t maximum)
{
    if(key == KEY_LEFT || key == KEY_RIGHT) {
        const uint8_t digit_count = 3;
        s_digit_index = (uint8_t)((s_digit_index + digit_count +
                         (key == KEY_RIGHT ? 1 : digit_count - 1)) % digit_count);
    } else if(key == KEY_UP || key == KEY_DOWN) {
        s_draft += key == KEY_UP ? steps[s_digit_index] : -steps[s_digit_index];
        if(s_draft < minimum) s_draft = minimum;
        if(s_draft > maximum) s_draft = maximum;
    }
}

/* 温度控件交互：上下/左右修改温度草稿，确认后提交目标温度命令。 */
static void handle_temperature_key(uint32_t key)
{
    static const int32_t steps[3] = {1000, 100, 10};
    if(key == KEY_OK) {
        if(app_service_submit(APP_SET_TEMP, s_draft)) {
            /* 设定值提交后立刻建立进度起点，等待温控器确认新目标。 */
            s_temp_progress_target_centi = s_draft;
            s_temp_progress_valid = 0;
            s_temp_progress_waiting = 1;
            if(s_view.temp.valid) {
                s_temp_progress_start_centi = s_view.temp.pv_centi;
                s_temp_progress_valid = 1;
            }
            s_edit = 0;
        }
    } else {
        edit_numeric_digits(key, steps, APP_TEMP_MIN_CENTI, APP_TEMP_MAX_CENTI);
    }
}

/* 时间控件交互：按分钟和秒位调整，确认后提交孵育时长命令。 */
static void handle_time_key(uint32_t key)
{
    static const int32_t steps[3] = {600, 60, 6};
    if(key == KEY_OK) {
        if(app_service_submit(APP_SET_TIME, s_draft)) {
            s_time_configured = 1;
            s_edit = 0;
        }
    } else {
        edit_numeric_digits(key, steps, (int32_t)APP_TIME_MIN_S, (int32_t)APP_TIME_MAX_S);
    }
}

/* 延迟控件交互：按百位、十位、个位调整，确认后提交降温延迟命令。 */
static void handle_delay_key(uint32_t key)
{
    static const int32_t steps[3] = {100, 10, 1};
    if(key == KEY_OK) {
        if(app_service_submit(APP_SET_DELAY, s_draft)) {
            s_delay_configured = 1;
            s_edit = 0;
        }
    } else {
        edit_numeric_digits(key, steps, 0, APP_DELAY_MAX_S);
    }
}

/* 进入温度编辑时，用当前目标温度初始化控件草稿。 */
static void begin_temperature_edit(void)
{
    /* 首次设定尚无目标值时，以当前有效温度作为编辑初值，避免从 00.0℃ 开始。 */
    s_draft = s_view.temp.target_valid ? s_view.run.target_centi :
              s_view.temp.valid ? s_view.temp.pv_centi : s_view.run.target_centi;
    s_digit_index = 2;
    s_edit = 1;
}

/* 进入时间编辑时，若当前值低于可设下限，先校正到最小有效时长。 */
static void begin_time_edit(void)
{
    s_draft = (int32_t)s_view.run.time_s;
    if(s_draft < (int32_t)APP_TIME_MIN_S) s_draft = (int32_t)APP_TIME_MIN_S;
    s_digit_index = 2;
    s_edit = 1;
}

/* 进入降温延迟编辑时，用当前延迟秒数初始化控件草稿。 */
static void begin_delay_edit(void)
{
    s_draft = (int32_t)s_view.run.delay_s;
    s_digit_index = 2;
    s_edit = 1;
}

/* 右侧开始控件只向应用服务提交开始请求，不直接控制温控硬件。 */
static void activate_start_control(void)
{
    printf("[ui] action START requested\r\n");
    app_service_submit(APP_START, 0);
}

/* 右侧停止控件只向应用服务提交停止请求。 */
static void activate_stop_control(void)
{
    printf("[ui] action STOP requested\r\n");
    app_service_submit(APP_STOP, 0);
}

/* 舱门控件根据应用快照决定提交开门还是关门请求。 */
static void activate_door_control(void)
{
    app_command_kind_t command = s_view.door == MOTOR_OPEN ? APP_CLOSE : APP_OPEN;
    printf("[ui] action %s requested (door_state=%d)\r\n",
           command == APP_CLOSE ? "CLOSE_DOOR" : "OPEN_DOOR", (int)s_view.door);
    app_service_submit(command, 0);
}

/* 消音控件只提交消音请求；硬件蜂鸣器由应用服务处理。 */
static void activate_mute_control(void)
{
    printf("[ui] action MUTE requested\r\n");
    app_service_submit(APP_MUTE, 0);
}

/* 按当前焦点把确认键路由到具体控件的编辑入口或动作入口。 */
static void activate_focused_control(void)
{
    /* 可聚焦不代表可执行；禁用时留在当前焦点，不提交应用命令。 */
    if(!allowed(s_focus_index)) {
        printf("[ui] focus=%s unavailable in current state\r\n",
               ui_focus_name(s_focus_index));
        return;
    }
    switch(s_focus_index) {
    case FOCUS_TEMPERATURE: begin_temperature_edit(); break;
    case FOCUS_TIME: begin_time_edit(); break;
    case FOCUS_DELAY: begin_delay_edit(); break;
    case FOCUS_START: activate_start_control(); break;
    case FOCUS_STOP: activate_stop_control(); break;
    case FOCUS_DOOR: activate_door_control(); break;
    case FOCUS_MUTE: activate_mute_control(); break;
    default: break;
    }
}

/* 编辑时只把事件交给当前的温度、时间或延迟控件。 */
static void route_edit_key(uint32_t key)
{
    switch(s_focus_index) {
    case FOCUS_TEMPERATURE: handle_temperature_key(key); break;
    case FOCUS_TIME: handle_time_key(key); break;
    case FOCUS_DELAY: handle_delay_key(key); break;
    default: break;
    }
}

/* 公共按键入口只负责事件分发；控件行为由对应处理函数完成。 */
void ui_layout_key(uint32_t key)
{
    uint8_t focus_before = s_focus_index;
    uint8_t editing_before = s_edit;
    if(!s_ok) {
        printf("[ui] key=%s ignored: UI not ready\r\n", ui_key_name(key));
        return;
    }
    printf("[ui] key=%s focus=%s edit=%u value=%ld\r\n",
           ui_key_name(key), ui_focus_name(s_focus_index), (unsigned)s_edit,
           (long)s_draft);
    if(!s_repeating && (key == KEY_UP || key == KEY_DOWN)) {
        s_repeat_key = key;
        s_hold_ms = s_repeat_ms = HAL_GetTick();
    }
    /* 蜂鸣时确认键优先消音，避免焦点位置影响报警操作。 */
    if(key == KEY_OK && s_view.run.buzzer) {
        activate_mute_control();
        return;
    }
    if(s_edit) {
        route_edit_key(key);
    } else if(key == KEY_OK) {
        activate_focused_control();
    } else if(key == KEY_LEFT || key == KEY_RIGHT || key == KEY_UP || key == KEY_DOWN) {
        nav(key);
    }
    focus_update();
    s_refresh_ms = HAL_GetTick() - 100;
    printf("[ui] key=%s handled: focus=%s->%s edit=%u->%u value=%ld\r\n",
           ui_key_name(key), ui_focus_name(focus_before), ui_focus_name(s_focus_index),
           (unsigned)editing_before, (unsigned)s_edit, (long)s_draft);
}

/* 计算编辑光标在数字字符串中的横坐标。 */
static int digit_x(const lv_font_t *font, const char *s, uint8_t index)
{
    int x = 8;
    uint8_t i;
    lv_font_glyph_dsc_t d;
    for(i = 0; i < index; ++i)
        if(lv_font_get_glyph_dsc(font, &d, s[i], s[i + 1])) x += d.adv_w;
    return x;
}

/* 读取当前用于界面显示的目标温度：优先采用已应用值，否则显示用户设定值。 */
static int32_t current_display_target(void)
{
    if(s_view.temp.target_valid) return s_view.temp.applied_centi;
    return s_view.temp.valid ? s_view.temp.pv_centi : 0;
}

/* 统一将温控器百分之一度读数四舍五入到界面显示的十分之一度。 */
static int32_t temperature_to_tenth(int32_t centi)
{
    return centi >= 0 ? (centi + 5) / 10 : -((-centi + 5) / 10);
}

/* 绘制顶部目标温度和当前设定值；这里只更新文字，不提交温控命令。 */
static void render_temperature_setpoint(int32_t display_target)
{
    char b[96];
    if(!s_view.temp.target_valid) {
        if(s_view.temp.valid) {
            int32_t tenth = temperature_to_tenth(s_view.temp.pv_centi);
            int32_t absolute = tenth < 0 ? -tenth : tenth;
            snprintf(b, sizeof(b), S_CURRENT_SHORT, (long)(tenth < 0 ? -absolute / 10 : absolute / 10),
                     (long)(absolute % 10));
            value(s_target, b);
        } else {
            value(s_target, S_CURRENT_SHORT_UNKNOWN);
        }
        value(s_sv, S_UNSET);
        return;
    }
    /* 编辑中小字同步显示正在调整的草稿，避免与主数值暂时不一致。 */
    if(s_edit && s_focus_index == FOCUS_TEMPERATURE) display_target = s_draft;
    snprintf(b, sizeof(b), S_TARGET_SHORT, (long)(display_target / 100),
             (long)((display_target % 100) / 10));
    value(s_target, b);
    snprintf(b, sizeof(b), S_SET_FMT, (long)(display_target / 100),
             (long)((display_target % 100) / 10));
    value(s_sv, b);
}

/* 刷新实时温度、颜色和温度控制/降温进度条。 */
static void render_temperature_widget(void)
{
    char number[24];
    uint32_t width = 0;
    int show_bar;
    int32_t t;
    inc_phase_t ph = s_view.run.phase;
    /* 编辑温度时显示草稿值；正常显示时对有效采样四舍五入到 0.1℃。 */
    t = (s_edit && s_focus_index == FOCUS_TEMPERATURE) ? s_draft : s_view.temp.pv_centi;
    if(s_edit && s_focus_index == FOCUS_TEMPERATURE) {
        snprintf(number, sizeof(number), "%02ld.%ld", (long)(t / 100), (long)((t % 100) / 10));
    } else if(s_view.temp.valid) {
        int32_t tenth = temperature_to_tenth(t);
        int32_t abs_t = tenth < 0 ? -tenth : tenth;
        snprintf(number, sizeof(number), "%s%ld.%ld", tenth < 0 ? "-" : "",
                 (long)(abs_t / 10), (long)(abs_t % 10));
    } else strcpy(number, "--.-");
    value(s_pv, number);
    {
        lv_font_glyph_dsc_t glyph;
        int32_t value_width = 0;
        uint8_t i;

        /* 按温度数值的实际字宽定位单位，保持“℃”与数字紧邻。 */
        for(i = 0; number[i] != '\0'; ++i) {
            if(lv_font_get_glyph_dsc(F28, &glyph, (uint32_t)(uint8_t)number[i],
                                     (uint32_t)(uint8_t)number[i + 1])) {
                value_width += glyph.adv_w;
            }
        }
    lv_obj_set_pos(s_temp_unit, 8 + value_width + 2, TEMP_Y + 29);
    }
    {
        uint32_t temp_color = !s_view.temp.valid ? RED :
            (ph == INC_COOL_REQUEST || ph == INC_COOLING) &&
            !s_view.at_temperature ? ORANGE : GREEN;
        tint(s_pv, temp_color);
        tint(s_temp_unit, temp_color);
    }
    /* 从确认温度设定开始，按起始温度到目标的实际变化计算进度。 */
    if((ph == INC_IDLE || ph == INC_RUNNING || ph == INC_DELAY) &&
       s_temp_progress_valid && s_view.temp.valid) {
        int64_t span = (int64_t)s_temp_progress_target_centi - s_temp_progress_start_centi;
        int64_t done = (int64_t)s_view.temp.pv_centi - s_temp_progress_start_centi;
        if(span == 0) {
            width = BAR_W;
        } else {
            if(span < 0) {
                span = -span;
                done = -done;
            }
            if(done < 0) done = 0;
            if(done > span) done = span;
            width = (uint32_t)(done * BAR_W / span);
        }
    } else if((ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE) &&
              s_view.temp.valid && s_view.run.target_centi > 400) {
        /* 自动降温时显示由孵育目标温度下降至 4℃ 的阶段进度。 */
        int32_t done = s_view.run.target_centi - s_view.temp.pv_centi;
        int32_t span = s_view.run.target_centi - 400;
        if(done < 0) done = 0;
        if(done > span) done = span;
        width = (uint32_t)((int64_t)done * BAR_W / span);
    }
    show_bar = s_view.temp.target_valid || incubator_model_active(&s_view.run);
    bar(s_temp_bar_track, s_temp_bar_fill, show_bar, width,
        (ph == INC_COOL_REQUEST || ph == INC_COOLING) ? BLUE : GREEN);
}

/* 报警时点亮顶部 ALM 标记，正常状态下使用弱化颜色。 */
static void render_alarm_indicator(void)
{
    tint(s_alarm, s_view.alarm ? RED : MUTED);
}

/* 绘制顶部舱门状态；运行卡片由 render_door_control() 单独负责。 */
static void render_header_door_status(void)
{
    /* 将电机状态映射为已开、已关、动作中、故障或未知。 */
    switch(s_view.door) {
    case MOTOR_OPEN: value(s_door, S_LID S_DOOR_OPEN); break;
    case MOTOR_CLOSED: value(s_door, S_LID S_DOOR_CLOSED); break;
    case MOTOR_INSTALLING:
        value(s_door, S_LID S_DOOR_INIT); break;
    case MOTOR_OPENING: case MOTOR_CLOSING:
        value(s_door, S_LID S_DOOR_MOVING); break;
    case MOTOR_FAULT: value(s_door, S_LID S_DOOR_FAULT); break;
    default: value(s_door, S_LID "--"); break;
    }
    tint(s_door, s_view.door_fault ? RED : GREEN);
}

/* 绘制顶部运行状态、报警文案、颜色和升降温图标。 */
static void render_phase_indicator(int32_t display_target)
{
    inc_phase_t ph = s_view.run.phase;
    const char *phase_txt;
    uint32_t phase_col;
    int heat = 0, cool = 0;
    /* 按报警、蜂鸣、运行阶段的优先级选择顶部状态文字和颜色。 */
    if(s_view.alarm) {
        /* 报警时按来源区分通讯、温控写入/传感器和舱门故障。 */
        phase_txt = (!s_view.temp.online || !s_view.temp.valid) ? S_COMM_ERROR :
                    (s_view.temp.alarm || s_view.write_fault) ? S_TEMP_ERROR : S_LID_ERROR;
        phase_col = RED;
    }
    else if(s_view.run.buzzer) {
        /* 蜂鸣提示优先于普通阶段提示，但低于需要立即识别的报警。 */
        phase_txt = S_BUZZ; phase_col = ORANGE;
    }
    else switch(ph) {
    case INC_RUNNING:
        /* 运行中按实际温度判断升温、降温或达到目标温度。 */
        if(s_view.at_temperature) { phase_txt = S_REACH; phase_col = GREEN; }
        else if(s_view.temp.pv_centi < display_target) { phase_txt = S_HEAT; phase_col = RED; heat = 1; }
        else { phase_txt = S_COOL; phase_col = BLUE; cool = 1; }
        break;
    case INC_DELAY:
        /* 延迟阶段维持目标温度；未到温时显示升温提示。 */
        phase_txt = s_view.at_temperature ? S_HOLD : S_HEAT;
        phase_col = s_view.at_temperature ? GREEN : RED;
        heat = s_view.at_temperature ? 0 : 1;
        break;
    case INC_COOL_REQUEST: case INC_COOLING:
        /* 自动降温请求和实际降温阶段共用蓝色降温提示。 */
        phase_txt = S_COOL; phase_col = BLUE; cool = 1; break;
    case INC_STORAGE:
        /* 保存阶段维持绿色提示，不显示升降温箭头。 */
        phase_txt = S_SAVE; phase_col = GREEN; break;
    case INC_FINISHED:
        /* 流程完成后显示结束状态。 */
        phase_txt = S_DONE; phase_col = MUTED; break;
    case INC_ERROR:
        /* 应用状态机错误以红色 ERROR 标记。 */
        phase_txt = "ERROR"; phase_col = RED; break;
    default:
        /* 空闲状态按传感器有效性、目标温度和恒温标志显示。 */
        if(!s_view.temp.valid) { phase_txt = S_WAIT; phase_col = MUTED; }
        else if(!s_view.temp.target_valid) { phase_txt = S_WAIT; phase_col = MUTED; }
        else if(s_view.at_temperature) { phase_txt = S_HOLD; phase_col = GREEN; }
        else if(s_view.temp.pv_centi < display_target) { phase_txt = S_HEAT; phase_col = RED; heat = 1; }
        else { phase_txt = S_COOL; phase_col = BLUE; cool = 1; }
        break;
    }
    value(s_phase, phase_txt);
    tint(s_phase, phase_col);
    icon_state(heat, cool, phase_col);
}

/* 刷新孵育时长、剩余时间及孵育计时进度；报警时不显示不可信的倒计时。 */
static void render_time_widget(void)
{
    char b[96], number[24];
    uint32_t width = 0, total, remain;
    int show_bar;
    inc_phase_t ph = s_view.run.phase;
    /* Completion remains visible throughout delay, cooling and storage. */
    int completed = ph == INC_FINISHED || ph == INC_DELAY ||
                    ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE;
    value(s_time_title, completed ? S_TIME_DONE_TITLE : S_TIME_TITLE);
    if(completed) {
        /* 完成态使用完整的 32px 行高，并给底部进度条留出间距。 */
        lv_obj_set_style_text_font(s_time, F32, 0);
        lv_obj_set_pos(s_time, 8, TIME_Y + 25);
        lv_obj_set_size(s_time, 184, 36);
        lv_obj_add_flag(s_time_aux, LV_OBJ_FLAG_HIDDEN);
    } else {
        /* 普通孵育时间恢复常规字号和位置。 */
        lv_obj_set_style_text_font(s_time, F28, 0);
        lv_obj_set_pos(s_time, 8, TIME_Y + 29);
        lv_obj_set_size(s_time, 184, 27);
        lv_obj_remove_flag(s_time_aux, LV_OBJ_FLAG_HIDDEN);
    }
    /* 空闲显示设定时长，运行显示剩余倒计时，报警时隐藏不可信数值。 */
    total = s_view.run.time_s;
    remain = (s_view.run.remaining_ms + 999u) / 1000u;
    if(s_edit && s_focus_index == FOCUS_TIME) {
        snprintf(number, sizeof(number), "%02lu.%lu min",
                 (unsigned long)(s_draft / 60), (unsigned long)((s_draft % 60) / 6));
    } else if(s_view.alarm) {
        strcpy(number, "--:--");
    } else if(ph == INC_RUNNING) {
        clock_text(number, sizeof(number), remain);
    } else if(ph == INC_IDLE) {
        snprintf(number, sizeof(number), "%02lu.%lu min",
                 (unsigned long)(total / 60), (unsigned long)((total % 60) / 6));
    } else strcpy(number, completed ? S_COMPLETED : "00:00");
    value(s_time, number);
    tint(s_time, s_view.alarm ? RED : ph == INC_RUNNING ? ORANGE : 0xD2ECDD);

    if(ph == INC_RUNNING) {
        /* 运行中辅助文字显示总设定时长，主数值显示剩余时间。 */
        if(s_view.run.paused) value(s_time_aux, S_PAUSED);
        else if(total < 3600u)
            snprintf(b, sizeof(b), S_REMAIN_MIN, (unsigned long)(total / 60u),
                     (unsigned long)((total % 60u) / 6u));
        else
            snprintf(b, sizeof(b), S_REMAIN_H, (unsigned long)(total / 3600u),
                     (unsigned long)((total % 3600u) / 360u));
        if(!s_view.run.paused) value(s_time_aux, b);
    } else if(ph == INC_IDLE) {
        /* 空闲时提示等待开始；结束、保存和错误阶段沿用暂停/结束状态文案。 */
        value(s_time_aux, S_WAIT);
    } else {
        value(s_time_aux, s_view.run.paused ? S_PAUSED : S_DONE);
    }

    if(ph == INC_RUNNING && s_view.run.run_duration_ms) {
        uint32_t elapsed = s_view.run.run_duration_ms > s_view.run.remaining_ms ?
                           s_view.run.run_duration_ms - s_view.run.remaining_ms : 0;
        width = (uint32_t)((uint64_t)elapsed * BAR_W / s_view.run.run_duration_ms);
    }
    else if(ph > INC_RUNNING) width = BAR_W;
    show_bar = s_time_configured || incubator_model_active(&s_view.run);
    bar(s_time_bar_track, s_time_bar_fill, show_bar, width,
        ph == INC_RUNNING ? ORANGE : GREEN);
}

/* 刷新自动降温延迟、提示文字及延迟/保存进度条。 */
static void render_delay_widget(void)
{
    char b[96], number[24];
    uint32_t width = 0;
    int show_bar;
    inc_phase_t ph = s_view.run.phase;
    /* 延迟阶段显示剩余秒数，降温阶段显示 0，未配置时用 N 表示关闭。 */
    if(s_edit && s_focus_index == FOCUS_DELAY) {
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
        strcpy(number, "N");   /* N 表示未设置自动降温延迟 */
    }
    value(s_delay, number);
    {
        lv_font_glyph_dsc_t glyph;
        int32_t value_width = 0;
        uint8_t i;
        /* 按延迟数字实际字宽定位秒单位，保持与主数值紧邻。 */
        for(i = 0; number[i] != '\0'; ++i) {
            if(lv_font_get_glyph_dsc(F28, &glyph, (uint32_t)(uint8_t)number[i],
                                     (uint32_t)(uint8_t)number[i + 1])) {
                value_width += glyph.adv_w;
            }
        }
        lv_obj_set_pos(s_delay_unit, 8 + value_width + 2, DELAY_Y + 30);
    }

    if(s_edit && s_focus_index == FOCUS_DELAY)
        snprintf(b, sizeof(b), "%lu s", (unsigned long)s_draft);
    else if(ph == INC_DELAY && s_view.run.delay_s)
        snprintf(b, sizeof(b), "%lu/%u",
                 (unsigned long)((s_view.run.delay_remaining_ms + 999u) / 1000u),
                 s_view.run.delay_s);
    else if(s_view.run.delay_s) snprintf(b, sizeof(b), "%u s", s_view.run.delay_s);
    else strcpy(b, "N");
    value(s_delay_set, b);

    /* 临时应用消息优先显示；否则按延迟、降温、保存、运行、空闲依次显示说明。 */
    if(s_view.message != APP_MSG_NONE) {
        const char *msg = s_view.message == APP_MSG_BUSY ? "BUSY" :
                          s_view.message == APP_MSG_SET_TEMP ? S_SET_TEMP_MSG :
                          s_view.message == APP_MSG_TEMP_OFFLINE ? S_COMM_ERROR :
                          s_view.message == APP_MSG_DOOR_FAILED ? S_LID_ERROR :
                          s_view.message == APP_MSG_SETTING_LOCKED ? S_STARTING : S_TEMP_ERROR;
        value(s_delay_state, msg);
        tint(s_delay_state, RED);
    } else if(s_edit && s_focus_index == FOCUS_DELAY) {
        value(s_delay_state, "");
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
        value(s_delay_state, "");
        tint(s_delay_state, MUTED);
    } else if(ph == INC_IDLE) {
        value(s_delay_state, "");
        tint(s_delay_state, MUTED);
    } else {
        value(s_delay_state, S_DONE);
        tint(s_delay_state, MUTED);
    }

    if(ph == INC_DELAY && s_view.run.run_delay_ms) {
        uint32_t elapsed = s_view.run.run_delay_ms > s_view.run.delay_remaining_ms ?
                           s_view.run.run_delay_ms - s_view.run.delay_remaining_ms : 0;
        width = (uint32_t)((uint64_t)elapsed * BAR_W / s_view.run.run_delay_ms);
    }
    else if(ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE)
        width = BAR_W;
    show_bar = s_delay_configured || incubator_model_active(&s_view.run);
    bar(s_delay_bar_track, s_delay_bar_fill, show_bar, width,
        (ph == INC_DELAY) ? ORANGE : BLUE);
}

/* 按运行阶段绘制开始卡片的文案和可用状态。 */
static void render_start_control(void)
{
    inc_phase_t ph = s_view.run.phase;
    /* 操作卡片文字反映当前状态；真正可操作性仍由 allowed() 统一控制。 */
    if(ph == INC_RUNNING || ph == INC_DELAY) {
        /* 运行/延迟期间开始键显示进行中；自动降温或保存阶段禁用开始。 */
        value(s_kt[ACTION_START], S_STARTING); tint(s_kt[ACTION_START], ORANGE);
        value(s_kn[ACTION_START], ""); tint(s_kn[ACTION_START], MUTED);
    } else if(ph == INC_COOL_REQUEST || ph == INC_COOLING || ph == INC_STORAGE) {
        value(s_kt[ACTION_START], S_START); tint(s_kt[ACTION_START], DISABLED);
        value(s_kn[ACTION_START], S_DONE); tint(s_kn[ACTION_START], DISABLED);
    } else if(ph == INC_IDLE) {
        /* 上电及普通待机均提示 SET，是否允许开始只影响文字颜色。 */
        uint32_t color = allowed(FOCUS_START) ? GREEN : DISABLED;
        value(s_kt[ACTION_START], S_START); tint(s_kt[ACTION_START], color);
        value(s_kn[ACTION_START], "SET"); tint(s_kn[ACTION_START], color);
    } else {
        value(s_kt[ACTION_START], S_START); tint(s_kt[ACTION_START], DISABLED);
        value(s_kn[ACTION_START], S_DONE); tint(s_kn[ACTION_START], DISABLED);
    }
}

/* 按运行阶段绘制停止卡片的文案和可用状态。 */
static void render_stop_control(void)
{
    inc_phase_t ph = s_view.run.phase;
    if(incubator_model_active(&s_view.run) || ph == INC_FINISHED || ph == INC_ERROR) {
        value(s_kt[ACTION_STOP], S_STOP); tint(s_kt[ACTION_STOP], GREEN);
        value(s_kn[ACTION_STOP], "SET"); tint(s_kn[ACTION_STOP], MUTED);
    } else {
        value(s_kt[ACTION_STOP], S_STOP); tint(s_kt[ACTION_STOP], DISABLED);
        value(s_kn[ACTION_STOP], "--"); tint(s_kn[ACTION_STOP], DISABLED);
    }
}

/* 按舱门状态绘制开盖/关盖、动作中和故障文案。 */
static void render_door_control(void)
{
    if(s_view.door == MOTOR_OPEN) {
        value(s_kt[ACTION_DOOR], S_CLOSE); tint(s_kt[ACTION_DOOR], GREEN);
        value(s_kn[ACTION_DOOR], S_DOOR_OPEN); tint(s_kn[ACTION_DOOR], MUTED);
    } else if(s_view.door == MOTOR_CLOSED) {
        value(s_kt[ACTION_DOOR], S_OPEN); tint(s_kt[ACTION_DOOR], GREEN);
        value(s_kn[ACTION_DOOR], S_DOOR_CLOSED); tint(s_kn[ACTION_DOOR], MUTED);
    } else if(s_view.door == MOTOR_OPENING || s_view.door == MOTOR_CLOSING ||
              s_view.door == MOTOR_INSTALLING) {
        value(s_kt[ACTION_DOOR], S_DOOR_MOVING); tint(s_kt[ACTION_DOOR], MUTED);
        value(s_kn[ACTION_DOOR], "--"); tint(s_kn[ACTION_DOOR], MUTED);
    } else if(s_view.door == MOTOR_FAULT) {
        value(s_kt[ACTION_DOOR], S_DOOR_FAULT); tint(s_kt[ACTION_DOOR], RED);
        value(s_kn[ACTION_DOOR], "SET"); tint(s_kn[ACTION_DOOR], MUTED);
    } else {
        value(s_kt[ACTION_DOOR], S_OPEN); tint(s_kt[ACTION_DOOR], GREEN);
        value(s_kn[ACTION_DOOR], "--"); tint(s_kn[ACTION_DOOR], MUTED);
    }
}

/* 按蜂鸣器状态绘制消音卡片的文案和颜色。 */
static void render_mute_control(void)
{
    if(s_view.run.buzzer) {
        value(s_kt[ACTION_MUTE], S_MUTE); tint(s_kt[ACTION_MUTE], RED);
        value(s_kn[ACTION_MUTE], "SET"); tint(s_kn[ACTION_MUTE], MUTED);
    } else {
        value(s_kt[ACTION_MUTE], S_MUTE); tint(s_kt[ACTION_MUTE], DISABLED);
        value(s_kn[ACTION_MUTE], S_NOT_BUZZ); tint(s_kn[ACTION_MUTE], DISABLED);
    }
}

/* 图标跟随运行状态及操作可用性着色；焦点选择不改变颜色。 */
static void render_action_icon_colors(void)
{
    inc_phase_t ph = s_view.run.phase;
    key_icons(ph == INC_RUNNING || ph == INC_DELAY ? ORANGE :
              (allowed(FOCUS_START) ? GREEN : DISABLED),
              (incubator_model_active(&s_view.run) || ph == INC_FINISHED || ph == INC_ERROR) ? GREEN : DISABLED,
              s_view.door == MOTOR_FAULT ? RED : (allowed(FOCUS_DOOR) ? GREEN : DISABLED),
              s_view.run.buzzer ? RED : DISABLED);
}

/* 在正在编辑的数字下方绘制光标；小数点不作为可编辑数字位。 */
static void refresh_edit_cursor(void)
{
    /* 只在参数编辑期间显示光标，离开编辑状态立即隐藏。 */
    if(s_edit) {
        char cursor[24];
        const lv_font_t *cf = F28;
        uint8_t ci = s_digit_index;   /* 当前数字字符在格式化字符串中的下标 */
        int x, y;
        lv_font_glyph_dsc_t d;
        if(s_focus_index == FOCUS_TEMPERATURE) {
            snprintf(cursor, sizeof(cursor), "%02ld.%ld", (long)(s_draft / 100), (long)((s_draft % 100) / 10));
            if(ci == 2) ++ci;       /* 跳过小数点，光标落在实际数字上 */
            y = TEMP_Y + 58;
        } else if(s_focus_index == FOCUS_TIME) {
            snprintf(cursor, sizeof(cursor), "%02lu.%lu", (unsigned long)(s_draft / 60),
                     (unsigned long)((s_draft % 60) / 6));
            if(ci == 2) ++ci;
            y = TIME_Y + 58;
        } else {
            snprintf(cursor, sizeof(cursor), "%03lu", (unsigned long)s_draft);
            cf = F28;
            y = DELAY_Y + 59;
        }
        x = digit_x(cf, cursor, ci);
        if(lv_font_get_glyph_dsc(cf, &d, cursor[ci], cursor[ci + 1]))
            lv_obj_set_width(s_digit, d.adv_w);
        lv_obj_set_pos(s_digit, x, y);
        lv_obj_remove_flag(s_digit, LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_digit, LV_OBJ_FLAG_HIDDEN);

}

/* 周期采集应用快照并分发到各区域刷新函数。 */
void ui_layout_process(void)
{
    uint32_t now = HAL_GetTick();

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

    /* 温控器确认新设定后捕获一次温度，后续待机和运行共用这一起点。 */
    if(s_temp_progress_waiting && !s_view.target_pending && s_view.temp.valid &&
       s_view.temp.target_valid && s_view.temp.applied_centi == s_temp_progress_target_centi) {
        s_temp_progress_start_centi = s_view.temp.pv_centi;
        s_temp_progress_valid = 1;
        s_temp_progress_waiting = 0;
    }
    /* 控件因状态变化变为禁用时，只退出编辑；焦点仍留在原位置。 */
    if(s_edit && !allowed(s_focus_index)) {
        s_edit = 0;
        focus_update();
    }

    /* 按固定顺序刷新各屏幕区域，便于按区域定位显示问题。 */
    {
        int32_t display_target = current_display_target();
        render_temperature_setpoint(display_target);
        render_temperature_widget();
        render_alarm_indicator();
        render_header_door_status();
        render_phase_indicator(display_target);
    }
    render_time_widget();
    render_delay_widget();
    render_start_control();
    render_stop_control();
    render_door_control();
    render_mute_control();
    action_icon_visibility();
    render_action_icon_colors();
    refresh_edit_cursor();
}
