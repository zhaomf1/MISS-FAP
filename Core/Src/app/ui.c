#include "ui.h"
#include <stdio.h>

#define UI_COL_BG 0xF5F7FA
#define UI_COL_CARD 0xDDEAF5
#define UI_COL_BLUE 0x2D78B8
#define UI_COL_TEXT 0x17324D
#define UI_COL_RED 0xE53935
#define UI_COL_GREEN 0x2E8B57

#define UI_CARD_TEMP_X 8
#define UI_CARD_TEMP_Y 38
#define UI_CARD_TEMP_W 188
#define UI_CARD_TEMP_H 91
#define UI_CARD_DOOR_X 202
#define UI_CARD_DOOR_Y 38
#define UI_CARD_DOOR_W 110
#define UI_CARD_DOOR_H 91

typedef enum { UI_F_TEMP = 0, UI_F_TIME, UI_F_DELAY, UI_F_START, UI_F_MUTE, UI_F_OPEN, UI_F_CLOSE, UI_F_COUNT } ui_focus_t;
typedef enum { UI_MODE_VIEW = 0, UI_MODE_EDIT } ui_mode_t;
static ui_focus_t s_focus;
static ui_mode_t s_mode;
static uint8_t s_temp_digit;
static int16_t s_target_temp = 370;
static uint16_t s_incubation_min = 15;
static uint16_t s_delay_sec = 60;
static lv_obj_t *s_focus_box, *s_temp, *s_target, *s_time, *s_delay, *s_status;

static lv_obj_t *label(lv_obj_t *p, const char *t, int x, int y, int w, int h, const lv_font_t *f, lv_color_t c)
{ lv_obj_t *o=lv_label_create(p); lv_label_set_text(o,t); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h); lv_obj_set_style_text_font(o,f,0); lv_obj_set_style_text_color(o,c,0); return o; }
static lv_obj_t *button(lv_obj_t *p, const char *t, int x, int y, int w, int h)
{ lv_obj_t *o=lv_button_create(p); lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h); lv_obj_set_style_bg_color(o,lv_color_hex(0xFFFFFF),0); lv_obj_set_style_border_width(o,1,0); lv_obj_set_style_border_color(o,lv_color_hex(UI_COL_BLUE),0); lv_obj_set_style_bg_color(o,lv_color_hex(0xB9D8F0),LV_STATE_FOCUSED); lv_obj_t *l=label(o,t,0,0,w,h,&lv_font_source_han_sans_sc_16_cjk,lv_color_hex(UI_COL_TEXT)); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0); return o; }
static void refresh(void)
{ char b[32]; lv_label_set_text_fmt(s_target,"目标温度：%d.%d℃",s_target_temp/10,(int)(s_target_temp%10)); lv_label_set_text_fmt(s_time,"孵育时间：%u min",s_incubation_min); lv_label_set_text_fmt(s_delay,"延迟转4℃：%u s",s_delay_sec); lv_label_set_text(s_status,s_mode==UI_MODE_EDIT?"编辑中":"待机"); lv_obj_set_pos(s_focus_box, s_focus==UI_F_TEMP?12:s_focus==UI_F_TIME?12:s_focus==UI_F_DELAY?12:205, s_focus==UI_F_TEMP?50:s_focus==UI_F_TIME?76:s_focus==UI_F_DELAY?102:184); lv_obj_set_size(s_focus_box,s_focus<=UI_F_DELAY?180:48, s_focus<=UI_F_DELAY?24:32); if(s_mode==UI_MODE_EDIT && s_focus==UI_F_TEMP){snprintf(b,sizeof(b),"温度位：%s",s_temp_digit==0?"十位":s_temp_digit==1?"个位":"小数位"); lv_label_set_text(s_status,b);} }
void ui_key_event(uint32_t key)
{ if(key==LV_KEY_LEFT){s_focus=(s_focus+UI_F_COUNT-1)%UI_F_COUNT; if(s_mode==UI_MODE_EDIT&&s_focus==UI_F_TEMP)s_temp_digit=(s_temp_digit+2)%3;} else if(key==LV_KEY_RIGHT){if(s_mode==UI_MODE_EDIT&&s_focus==UI_F_TEMP)s_temp_digit=(s_temp_digit+1)%3;else s_focus=(s_focus+1)%UI_F_COUNT;} else if(key==LV_KEY_UP||key==LV_KEY_DOWN){int d=key==LV_KEY_UP?1:-1; if(s_mode==UI_MODE_EDIT&&s_focus==UI_F_TEMP){if(s_temp_digit==0)s_target_temp+=d*10;else if(s_temp_digit==1)s_target_temp+=d;else s_target_temp+=d;} else if(s_mode==UI_MODE_EDIT&&s_focus==UI_F_TIME){if(d>0)s_incubation_min++;else if(s_incubation_min)s_incubation_min--;} else if(s_mode==UI_MODE_EDIT&&s_focus==UI_F_DELAY){if(d>0)s_delay_sec+=5;else if(s_delay_sec>=5)s_delay_sec-=5;}} else if(key==LV_KEY_ENTER){if(s_focus<=UI_F_DELAY)s_mode=s_mode==UI_MODE_VIEW?UI_MODE_EDIT:UI_MODE_VIEW;else if(s_focus==UI_F_START)lv_label_set_text(s_status,"已开始");else if(s_focus==UI_F_MUTE)lv_label_set_text(s_status,"已消音");else if(s_focus==UI_F_OPEN)lv_label_set_text(s_status,"开盖中");else if(s_focus==UI_F_CLOSE)lv_label_set_text(s_status,"关盖中");} refresh(); }
static void ui_init_legacy(void)
#if 0
{ }
#endif
