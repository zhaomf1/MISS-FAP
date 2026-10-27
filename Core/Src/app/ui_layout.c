#include "ui.h"
#include "key.h"
#include "app_service.h"
#include "lv_font_ui_font.h"
#include <stdio.h>
#include <string.h>

/*
 * 主界面布局模块：负责温度、目标值、运行状态、舱门状态和按键焦点显示。
 * 本模块只提交 app_service 命令，不直接执行温控器或电机操作。
 */

/*
 * 界面文案以 UTF-8 字节转义写入 C 字符串；转义不会在屏幕上显示为十六进制。
 * 这样可避开 Keil ARMCC 5 在不同 Windows 代码页下解析中文源码字符串不一致的问题。
 */
#define S_LID          "\xE7\x9B\x96:"                                      /* 顶部舱门状态前缀 */
#define S_DOOR_CLOSED  "\xE5\xB7\xB2\xE5\x85\xB3"                        /* 舱门已关闭 */
#define S_DOOR_OPEN    "\xE5\xB7\xB2\xE5\xBC\x80"                        /* 舱门已打开 */
#define S_DOOR_MOVING  "\xE5\x8A\xA8\xE4\xBD\x9C\xE4\xB8\xAD"  /* 舱门电机动作中 */
#define S_DOOR_FAULT   "\xE6\x95\x85\xE9\x9A\x9C"                        /* 舱门故障 */
#define S_HEAT         "\xE5\x8D\x87\xE6\xB8\xA9"                        /* 温度低于目标，正在升温 */
#define S_COOL         "\xE9\x99\x8D\xE6\xB8\xA9"                        /* 温度高于目标，正在降温 */
#define S_HOLD         "\xE6\x81\x92\xE6\xB8\xA9\xE4\xB8\xAD"  /* 恒温中 */
#define S_REACH        "\xE8\xBE\xBE\xE6\xA0\x87"                        /* 已达到目标温度 */
#define S_BUZZ         "\xE8\x9C\x82\xE9\xB8\xA3"                        /* 蜂鸣器正在提示 */
#define S_ALARM        "\xE6\x8A\xA5\xE8\xAD\xA6"                        /* 报警状态 */
#define S_SAVE         "\xE4\xBF\x9D\xE5\xAD\x98"                        /* 保存阶段 */
#define S_TEMP_TITLE   "\xE6\xB8\xA9\xE5\xBA\xA6 TEMP"                  /* 实时温度区标题 */
#define S_TARGET_TITLE "\xE7\x9B\xAE\xE6\xA0\x87 TEMP"                  /* 目标温度标题 */
#define S_TARGET_FMT   "\xE7\x9B\xAE\xE6\xA0\x87 %ld.%ld \xE2\x84\x83" /* 目标温度格式：整数、小数 */
#define S_TIME_TITLE   "\xE5\xAD\xB5\xE8\x82\xB2\xE6\x97\xB6\xE9\x97\xB4 TIME" /* 孵育时间区标题 */
#define S_WAIT         "\xE5\xBE\x85\xE5\xBC\x80\xE5\xA7\x8B"  /* 等待用户开始 */
#define S_DONE         "\xE5\xB7\xB2\xE7\xBB\x93\xE6\x9D\x9F"  /* 流程已结束 */
#define S_PAUSED       "\xE5\xB7\xB2\xE6\x9A\x82\xE5\x81\x9C"  /* 计时已暂停 */
#define S_REMAIN_MIN   "\xE5\x89\xA9\xE4\xBD\x99 \xE5\x85\xB1 %lu.%lu min" /* 分钟格式：剩余总时长 */
#define S_REMAIN_H     "\xE5\x89\xA9\xE4\xBD\x99 \xE5\x85\xB1 %lu.%lu h"   /* 小时格式：剩余总时长 */
#define S_DELAY_TITLE  "\xE5\xBB\xB6\xE8\xBF\x9F 4\xE2\x84\x83 \xE4\xBF\x9D\xE5\xAD\x98" /* 延迟降温设置标题 */
#define S_EDITABLE     "\xE5\x8F\xAF\xE7\xBC\x96\xE8\xBE\x91"  /* 当前控件可编辑 */
#define S_AFTER        "%lu s \xE5\x90\x8E\xE9\x99\x8D\xE6\xB8\xA9" /* N 秒后开始降温 */
#define S_COOLING      "\xE9\x99\x8D\xE6\xB8\xA9\xE4\xB8\xAD"  /* 自动降温进行中 */
#define S_KEEP         "\xE4\xBF\x9D\xE5\xAD\x98\xE4\xB8\xAD \xE7\x9B\xB4\xE8\x87\xB3\xE5\x8F\x96\xE6\x9D\xBF" /* 持续保存直到取板 */
#define S_START        "\xE5\xBC\x80\xE5\xA7\x8B"                        /* 开始操作 */
#define S_STARTING     "\xE8\xAE\xA1\xE6\x97\xB6\xE4\xB8\xAD"  /* 正在计时/运行 */
#define S_STOP         "\xE5\x81\x9C\xE6\xAD\xA2"                        /* 停止操作 */
#define S_OPEN         "\xE5\xBC\x80\xE7\x9B\x96"                        /* 打开舱盖操作 */
#define S_CLOSE        "\xE5\x85\xB3\xE7\x9B\x96"                        /* 关闭舱盖操作 */
#define S_MUTE         "\xE6\xB6\x88\xE9\x9F\xB3"                        /* 消音操作 */
#define S_NOT_BUZZ     "\xE6\x9C\xAA\xE8\x9C\x82\xE9\xB8\xA3"  /* 当前没有蜂鸣提示 */
#define S_SET_FMT      "\xE8\xAE\xBE\xE5\xAE\x9A %ld.%ld"          /* 顶部设定温度格式 */
#define S_CURRENT_FMT  "\xE5\xBD\x93\xE5\x89\x8D %ld.%ld\xE2\x84\x83" /* 尚未设定目标时显示实时温度 */
#define S_CURRENT_UNKNOWN "\xE5\xBD\x93\xE5\x89\x8D --.-\xE2\x84\x83" /* 实时温度尚无有效读数 */
#define S_UNSET        "\xE6\x9C\xAA\xE8\xAE\xBE\xE5\xAE\x9A" /* 尚未设置目标温度 */
#define S_COMM_ERROR   "\xE9\x80\x9A\xE8\xAE\xAF\xE5\xBC\x82\xE5\xB8\xB8" /* 温度通信异常 */
#define S_TEMP_ERROR   "\xE6\xB8\xA9\xE6\x8E\xA7\xE5\xBC\x82\xE5\xB8\xB8" /* 温控或写入异常 */
#define S_LID_ERROR    "\xE5\xBC\x80\xE7\x9B\x96\xE5\xBC\x82\xE5\xB8\xB8" /* 舱门异常 */
#define S_SET_TEMP_MSG "\xE8\xAE\xBE\xE5\xAE\x9A\xE6\xB8\xA9\xE5\xBA\xA6" /* 正在应用温度设定 */

/* 保持字库位图数据为只读；小型描述符副本为字库子集缺少的空格和标点提供拉丁字母回退。 */
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
#define TEMP_H   64
#define TIME_Y   88
#define TIME_H   64
#define DELAY_Y  152
#define DELAY_H  88
#define BAR_W    184

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
static lv_obj_t *s_pv, *s_target, *s_time, *s_time_aux, *s_delay;
static lv_obj_t *s_delay_set, *s_delay_state;
static lv_obj_t *s_alarm, *s_door, *s_phase, *s_sv;
static lv_obj_t *s_kt[ACTION_COUNT], *s_kn[ACTION_COUNT];       /* 四个操作卡片的主文字和辅助文字 */
static lv_obj_t *s_ic_play[3], *s_ic_stop, *s_ic_door, *s_ic_mute[3];
static lv_obj_t *s_ic_up[3], *s_ic_down[3], *s_ic_dot;
static lv_obj_t *s_temp_bar, *s_time_bar, *s_delay_bar;
static lv_obj_t *s_focus, *s_digit;
static uint8_t s_ok, s_focus_index, s_edit, s_digit_index;
static int32_t s_draft;
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

/* 创建只有边框的图标；透明填充可以避免正方形停止图标与操作区域背景混成一块。 */
static lv_obj_t *outline_rect(lv_obj_t *p, int x, int y, int size,
                              uint32_t color, int border)
{
    lv_obj_t *o = lv_obj_create(p);
    if(!o) { s_ok = 0; return NULL; }
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, size, size);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_border_width(o, border, 0);
    lv_obj_set_style_radius(o, 0, 0);
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

/* 更新进度条填充宽度；宽度为 0 时隐藏填充对象。 */
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

/* 图标只在对应操作焦点有效时显示，避免四个符号同时挤在卡片交界处。 */
static void action_icon_visibility(void)
{
    uint8_t i;
    int show_start = s_focus_index == FOCUS_START && allowed(FOCUS_START);
    int show_stop = s_focus_index == FOCUS_STOP && allowed(FOCUS_STOP);
    int show_door = s_focus_index == FOCUS_DOOR && allowed(FOCUS_DOOR);
    int show_mute = s_focus_index == FOCUS_MUTE && allowed(FOCUS_MUTE);

    for(i = 0; i < 3; ++i) {
        if(show_start) lv_obj_remove_flag(s_ic_play[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_ic_play[i], LV_OBJ_FLAG_HIDDEN);
        if(show_mute) lv_obj_remove_flag(s_ic_mute[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_ic_mute[i], LV_OBJ_FLAG_HIDDEN);
    }
    if(show_stop) lv_obj_remove_flag(s_ic_stop, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ic_stop, LV_OBJ_FLAG_HIDDEN);
    if(show_door) lv_obj_remove_flag(s_ic_door, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ic_door, LV_OBJ_FLAG_HIDDEN);
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
    if(s_ic_dot) {
        if(up || down) lv_obj_add_flag(s_ic_dot, LV_OBJ_FLAG_HIDDEN);
        else {
            lv_obj_remove_flag(s_ic_dot, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_color(s_ic_dot, lv_color_hex(color), 0);
        }
    }
}

/* 设置操作图标的颜色。 */
static void key_icon_color(lv_obj_t *o, uint32_t c)
{
    if(o) lv_obj_set_style_bg_color(o, lv_color_hex(c), 0);
}

/* 统一更新开始、停止、舱门和消音图标的状态颜色。 */
static void key_icons(uint32_t start_c, uint32_t stop_c,
                      uint32_t door_c, uint32_t mute_c)
{
    int i;
    for(i = 0; i < 3; ++i) {
        key_icon_color(s_ic_play[i], start_c);
        key_icon_color(s_ic_mute[i], mute_c);
    }
    /* 停止图标是边框样式，因此更新边框颜色而不是背景填充色。 */
    if(s_ic_stop) lv_obj_set_style_border_color(s_ic_stop, lv_color_hex(stop_c), 0);
    if(s_ic_door) lv_obj_set_style_border_color(s_ic_door, lv_color_hex(door_c), 0);
}

/* 创建左侧信息区、分隔线和三条进度条的底轨。 */
static void create_page_frame(lv_obj_t *screen)
{
    rect(screen, 0, 0, SCREEN_WIDTH, HEADER_H, HEADER, 0);
    rect(screen, 0, HEADER_H - 1, SCREEN_WIDTH, 1, LINE, 0);
    rect(screen, LEFT_W, HEADER_H, 1, SCREEN_HEIGHT - HEADER_H, LINE, 0);
    rect(screen, 0, TIME_Y - 1, LEFT_W, 1, LINE, 0);
    rect(screen, 0, DELAY_Y - 1, LEFT_W, 1, LINE, 0);

    /* 右侧四个操作卡片按 2 列 2 行排列。 */
    rect(screen, ACTION_LEFT_X, ACTION_TOP_Y, ACTION_LEFT_W, ACTION_ROW_H, 0x143523, 1);
    rect(screen, ACTION_RIGHT_X, ACTION_TOP_Y, ACTION_RIGHT_W, ACTION_ROW_H, 0x102B1D, 1);
    rect(screen, ACTION_LEFT_X, ACTION_LOW_Y, ACTION_LEFT_W, ACTION_ROW_H, BG, 1);
    rect(screen, ACTION_RIGHT_X, ACTION_LOW_Y, ACTION_RIGHT_W, ACTION_ROW_H, 0x10251A, 1);

    rect(screen, 8, TEMP_Y + 62, BAR_W, 2, 0x263C2F, 0);
    s_temp_bar = rect(screen, 8, TEMP_Y + 62, 0, 2, GREEN, 0);
    rect(screen, 8, TIME_Y + 59, BAR_W, 4, 0x263C2F, 0);
    s_time_bar = rect(screen, 8, TIME_Y + 59, 0, 4, GREEN, 0);
    rect(screen, 8, DELAY_Y + 84, BAR_W, 3, 0x263C2F, 0);
    s_delay_bar = rect(screen, 8, DELAY_Y + 84, 0, 3, YELLOW, 0);
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
    text(screen, S_TEMP_TITLE, 8, 26, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_pv = text(screen, "--.-", 8, 40, 106, 32, F28, GREEN, LV_TEXT_ALIGN_LEFT);
    text(screen, "\xE2\x84\x83", 118, 52, 24, 16, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_target = text(screen, "", 8, 72, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
}

/* 创建孵育时间主数值和剩余时间/运行状态说明。 */
static void create_timing_area(lv_obj_t *screen)
{
    text(screen, S_TIME_TITLE, 8, 89, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_time = text(screen, "15.0 min", 8, 103, 184, 31, F28, 0xD2ECDD, LV_TEXT_ALIGN_LEFT);
    s_time_aux = text(screen, S_WAIT, 8, 134, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
}

/* 创建自动降温延迟设置、倒计时和状态提示区域。 */
static void create_delay_area(lv_obj_t *screen)
{
    text(screen, S_DELAY_TITLE, 8, 153, 124, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_delay_set = text(screen, "", 140, 153, 52, 14, F12, MUTED, LV_TEXT_ALIGN_RIGHT);
    s_delay = text(screen, "0", 8, 170, 60, 34, F28, YELLOW, LV_TEXT_ALIGN_LEFT);
    text(screen, "s", 74, 185, 14, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
    s_delay_state = text(screen, S_EDITABLE, 8, 207, 184, 14, F12, MUTED, LV_TEXT_ALIGN_LEFT);
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
    s_ic_dot = rect(screen, 106, 9, 4, 4, GREEN, 0);

    /* 播放箭头用三个实心矩形拼接；停止符号固定为方形空心边框。 */
    s_ic_play[0] = rect(screen, ICON_PLAY_X, ICON_PLAY_Y, 3, 12, GREEN, 0);
    s_ic_play[1] = rect(screen, ICON_PLAY_X + 3, ICON_PLAY_Y + 3, 3, 6, GREEN, 0);
    s_ic_play[2] = rect(screen, ICON_PLAY_X + 6, ICON_PLAY_Y + 5, 3, 2, GREEN, 0);
    s_ic_stop = outline_rect(screen, ICON_STOP_X, ICON_STOP_Y, ACTION_ICON_SIZE, GREEN, 2);

    /* 舱门图标与停止符号分列；消音图标移到下排消音卡片内。 */
    s_ic_door = rect(screen, ICON_DOOR_X, ICON_DOOR_Y, 13, 11, BG, 2);
    if(s_ic_door) lv_obj_set_style_bg_opa(s_ic_door, LV_OPA_TRANSP, 0);
    s_ic_mute[0] = rect(screen, ICON_MUTE_X, ICON_MUTE_Y + 4, 3, 4, GREEN, 0);
    s_ic_mute[1] = rect(screen, ICON_MUTE_X + 4, ICON_MUTE_Y, 3, 8, GREEN, 0);
    s_ic_mute[2] = rect(screen, ICON_MUTE_X + 8, ICON_MUTE_Y - 4, 3, 12, GREEN, 0);
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
    s_font12 = lv_font_ui_font_12; s_font12.fallback = &lv_font_montserrat_14;
    s_font14 = lv_font_ui_font_14; s_font14.fallback = &lv_font_montserrat_14;
    s_font28 = lv_font_ui_font_28; s_font28.fallback = &lv_font_montserrat_20;
    s_ok = 1;
    s_focus_index = FOCUS_TEMPERATURE;
    s_edit = 0; s_digit_index = 0;

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

/* 按方向键在可用焦点之间循环导航，跳过当前不可用项。 */
static void nav(uint32_t key)
{
    static const uint8_t right[FOCUS_COUNT] = {
        FOCUS_START, FOCUS_DOOR, FOCUS_MUTE, FOCUS_STOP,
        FOCUS_TEMPERATURE, FOCUS_MUTE, FOCUS_DELAY
    };
    static const uint8_t left[FOCUS_COUNT] = {
        FOCUS_STOP, FOCUS_MUTE, FOCUS_MUTE, FOCUS_TEMPERATURE,
        FOCUS_START, FOCUS_TIME, FOCUS_DOOR
    };
    static const uint8_t up[FOCUS_COUNT] = {
        FOCUS_DELAY, FOCUS_TEMPERATURE, FOCUS_TIME, FOCUS_DOOR,
        FOCUS_MUTE, FOCUS_START, FOCUS_STOP
    };
    static const uint8_t down[FOCUS_COUNT] = {
        FOCUS_TIME, FOCUS_DELAY, FOCUS_TEMPERATURE, FOCUS_DOOR,
        FOCUS_MUTE, FOCUS_START, FOCUS_STOP
    };
    const uint8_t *map = key == KEY_RIGHT ? right : key == KEY_LEFT ? left :
                         key == KEY_UP ? up : down;
    uint8_t next = map[s_focus_index];
    uint8_t visited[FOCUS_COUNT] = {0};
    uint8_t steps;

    /* 几何导航表可能在不可用控件间形成闭环，记录访问项后再做顺序兜底。 */
    for(steps = 0; steps < FOCUS_COUNT && !allowed(next) && !visited[next]; ++steps) {
        visited[next] = 1;
        next = map[next];
    }
    if(!allowed(next) || next == s_focus_index) {
        int direction = (key == KEY_RIGHT || key == KEY_DOWN) ? 1 : -1;
        for(steps = 1; steps <= FOCUS_COUNT; ++steps) {
            int candidate = (int)s_focus_index + direction * (int)steps;
            while(candidate < 0) candidate += FOCUS_COUNT;
            candidate %= FOCUS_COUNT;
            if(allowed((uint8_t)candidate)) {
                next = (uint8_t)candidate;
                break;
            }
        }
    }
    if(allowed(next)) {
        s_focus_index = next;
        s_edit = 0;
        focus_update();
    }
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
        if(app_service_submit(APP_SET_TEMP, s_draft)) s_edit = 0;
    } else {
        edit_numeric_digits(key, steps, APP_TEMP_MIN_CENTI, APP_TEMP_MAX_CENTI);
    }
}

/* 时间控件交互：按分钟和秒位调整，确认后提交孵育时长命令。 */
static void handle_time_key(uint32_t key)
{
    static const int32_t steps[3] = {600, 60, 6};
    if(key == KEY_OK) {
        if(app_service_submit(APP_SET_TIME, s_draft)) s_edit = 0;
    } else {
        edit_numeric_digits(key, steps, (int32_t)APP_TIME_MIN_S, (int32_t)APP_TIME_MAX_S);
    }
}

/* 延迟控件交互：按百位、十位、个位调整，确认后提交降温延迟命令。 */
static void handle_delay_key(uint32_t key)
{
    static const int32_t steps[3] = {100, 10, 1};
    if(key == KEY_OK) {
        if(app_service_submit(APP_SET_DELAY, s_draft)) s_edit = 0;
    } else {
        edit_numeric_digits(key, steps, 0, APP_DELAY_MAX_S);
    }
}

/* 进入温度编辑时，用当前目标温度初始化控件草稿。 */
static void begin_temperature_edit(void)
{
    s_draft = s_view.run.target_centi;
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

/* 绘制顶部目标温度和当前设定值；这里只更新文字，不提交温控命令。 */
static void render_temperature_setpoint(int32_t display_target)
{
    char b[96];
    if(!s_view.temp.target_valid) {
        if(s_view.temp.valid) {
            snprintf(b, sizeof(b), S_CURRENT_FMT, (long)(s_view.temp.pv_centi / 100),
                     (long)((s_view.temp.pv_centi % 100) / 10));
            value(s_target, b);
        } else {
            value(s_target, S_CURRENT_UNKNOWN);
        }
        value(s_sv, S_UNSET);
        return;
    }
    snprintf(b, sizeof(b), S_TARGET_FMT, (long)(display_target / 100),
             (long)((display_target % 100) / 10));
    value(s_target, b);
    snprintf(b, sizeof(b), S_SET_FMT, (long)(display_target / 100),
             (long)((display_target % 100) / 10));
    value(s_sv, b);
}

/* 绘制温度控件的实时数值、颜色和降温进度条。 */
static void render_temperature_widget(void)
{
    char number[24];
    uint32_t width;
    int32_t t;
    inc_phase_t ph = s_view.run.phase;
    /* 编辑温度时显示草稿值；正常显示时对有效采样四舍五入到 0.1℃。 */
    t = (s_edit && s_focus_index == FOCUS_TEMPERATURE) ? s_draft : s_view.temp.pv_centi;
    if(s_edit && s_focus_index == FOCUS_TEMPERATURE) {
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
    case MOTOR_OPENING: case MOTOR_CLOSING: case MOTOR_INSTALLING:
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

/* 刷新孵育时长、剩余时间和运行进度；报警时不显示不可信的倒计时。 */
static void render_time_widget(void)
{
    char b[96], number[24];
    uint32_t width, total, remain;
    inc_phase_t ph = s_view.run.phase;
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
    } else strcpy(number, "00:00");
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

    width = 0;
    if(ph == INC_RUNNING && s_view.run.run_duration_ms)
        width = (uint32_t)((uint64_t)(s_view.run.run_duration_ms - s_view.run.remaining_ms) *
                           BAR_W / s_view.run.run_duration_ms);
    else if(ph > INC_RUNNING) width = BAR_W;
    bar(NULL, s_time_bar, width, GREEN);

}

/* 刷新自动降温延迟、提示文字和延迟/降温进度条。 */
static void render_delay_widget(void)
{
    char b[96], number[24];
    uint32_t width;
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
    } else if(ph == INC_IDLE && allowed(FOCUS_START)) {
        value(s_kt[ACTION_START], S_START); tint(s_kt[ACTION_START], GREEN);
        value(s_kn[ACTION_START], "SET"); tint(s_kn[ACTION_START], MUTED);
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

/* 图标只表现状态颜色；图标显示/隐藏由焦点规则单独处理。 */
static void render_action_icon_colors(void)
{
    inc_phase_t ph = s_view.run.phase;
    key_icons(ph == INC_RUNNING || ph == INC_DELAY ? ORANGE :
              (ph == INC_IDLE ? GREEN : DISABLED),
              (incubator_model_active(&s_view.run) || ph == INC_FINISHED || ph == INC_ERROR) ? GREEN : DISABLED,
              s_view.door == MOTOR_FAULT ? RED : GREEN,
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
            y = 70;
        } else if(s_focus_index == FOCUS_TIME) {
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

    /* 状态变化后，若当前焦点已不可用，自动跳转到第一个可用控件。 */
    if(!allowed(s_focus_index)) {
        uint8_t i;
        s_edit = 0;
        /* 当前控件因运行状态变为禁用时，优先落到后续最近的可用控件。
         * 例如开始后从 START 顺移到 STOP，而不是跳回左侧第一个字段。 */
        for(i = 1; i <= FOCUS_COUNT; ++i) {
            uint8_t candidate = (uint8_t)((s_focus_index + i) % FOCUS_COUNT);
            if(allowed(candidate)) { s_focus_index = candidate; break; }
        }
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
    render_action_icon_colors();
    refresh_edit_cursor();
}
