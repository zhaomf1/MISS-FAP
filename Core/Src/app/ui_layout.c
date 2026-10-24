#include "ui.h"
#include "key.h"
#include "app_service.h"
#include "lv_font_ui_font.h"
#include <stdio.h>
#include <string.h>

/* Keep supplied bitmap data const; small descriptor copies add a Latin
 * fallback for spaces/punctuation absent from the supplied subset. */
static lv_font_t s_font12, s_font14, s_font28, s_font32;
#define F12 (&s_font12)
#define F14 (&s_font14)
#define F28 (&s_font28)
#define F32 (&s_font32)
#define BG 0x081B12
#define LINE 0x244B36
#define GREEN 0x6FFFA7
#define MUTED 0x739C88
#define YELLOW 0xFFD54A
#define RED 0xFF7770

/* UI owns handles/edit state only. No blocking serial transactions here. */
static lv_obj_t *s_pv, *s_target, *s_time, *s_delay, *s_delay_set;
static lv_obj_t *s_alarm, *s_heat, *s_door, *s_sv, *s_phase, *s_notice;
static lv_obj_t *s_mute, *s_mute_note, *s_focus, *s_digit, *s_progress;
static lv_obj_t *s_temp_title;
static uint8_t s_ok, s_focus_index, s_edit, s_digit_index;
static int32_t s_draft;
static app_snapshot_t s_view;
static uint32_t s_refresh_ms;
static uint32_t s_repeat_key=KEY_COUNT, s_hold_ms, s_repeat_ms;
static uint8_t s_repeating;

static lv_obj_t *rect(lv_obj_t *p, int x, int y, int w, int h,
                       uint32_t color, int border)
{
    lv_obj_t *o = lv_obj_create(p);
    if(!o) { s_ok=0; return NULL; }
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_obj_set_style_bg_color(o,lv_color_hex(color),0);
    lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_set_style_border_color(o,lv_color_hex(LINE),0);
    lv_obj_set_style_border_width(o,border,0);
    lv_obj_set_style_pad_all(o,0,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE);
    return o;
}

static lv_obj_t *text(lv_obj_t *p, const char *v, int x,int y,int w,int h,
                      const lv_font_t *font,uint32_t color,lv_text_align_t align)
{
    lv_obj_t *o=lv_label_create(p);
    if(!o) { s_ok=0; return NULL; }
    lv_obj_remove_style_all(o);
    lv_obj_set_style_text_font(o,font,0);
    lv_obj_set_style_text_color(o,lv_color_hex(color),0);
    lv_obj_set_style_text_align(o,align,0);
    lv_obj_set_style_text_letter_space(o,0,0);
    lv_obj_set_style_text_line_space(o,0,0);
    lv_label_set_long_mode(o,LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(o,x,y); lv_obj_set_size(o,w,h);
    lv_label_set_text(o,v);
    return o;
}

/* Avoid per-refresh allocation/invalidation when the visible value is unchanged. */
static void value(lv_obj_t *o,const char *v)
{
    if(o && strcmp(lv_label_get_text(o),v)!=0) lv_label_set_text(o,v);
}

static void focus_update(void)
{
    static const int bounds[7][4]={{1,25,198,82},{1,109,198,78},{1,189,198,50},
        {202,25,57,70},{261,25,58,70},{202,97,57,70},{261,97,58,70}};
    const int *b=bounds[s_focus_index];
    lv_obj_set_pos(s_focus,b[0],b[1]); lv_obj_set_size(s_focus,b[2],b[3]);
    lv_obj_set_style_border_color(s_focus,lv_color_hex(s_edit?YELLOW:GREEN),0);
}

void ui_layout_init(void)
{
    lv_obj_t *s=lv_screen_active();
    s_font12=lv_font_ui_font_12; s_font12.fallback=&lv_font_montserrat_14;
    s_font14=lv_font_ui_font_14; s_font14.fallback=&lv_font_montserrat_14;
    s_font28=lv_font_ui_font_28; s_font28.fallback=&lv_font_montserrat_20;
    s_font32=lv_font_ui_font_32; s_font32.fallback=&lv_font_montserrat_20;
    s_ok=1; s_focus_index=0; s_edit=0; s_digit_index=0;
    lv_obj_clean(s); lv_obj_remove_style_all(s);
    lv_obj_set_size(s,320,240);
    lv_obj_set_style_bg_color(s,lv_color_hex(BG),0);
    lv_obj_set_style_bg_opa(s,LV_OPA_COVER,0);
    lv_obj_clear_flag(s,LV_OBJ_FLAG_SCROLLABLE);
    /* Structural objects precede every label. No opaque overlay over text. */
    rect(s,0,0,320,24,0x06140D,0);
    rect(s,0,23,320,1,LINE,0); rect(s,200,24,1,216,LINE,0);
    rect(s,0,108,200,1,LINE,0); rect(s,0,188,200,1,LINE,0);
    rect(s,201,24,59,72,0x143523,1); rect(s,260,24,60,72,0x102B1D,1);
    rect(s,201,96,59,72,BG,1); rect(s,260,96,60,72,0x10251A,1);
    rect(s,8,239,184,1,0x263C2F,0);
    s_progress=rect(s,8,239,184,1,YELLOW,0);
    s_focus=rect(s,1,25,198,82,BG,1);
    if(!s_ok) return;
    lv_obj_set_style_bg_opa(s_focus,LV_OPA_TRANSP,0);
    s_digit=rect(s,8,88,12,2,YELLOW,0);
    if(!s_ok) return;
    lv_obj_add_flag(s_digit,LV_OBJ_FLAG_HIDDEN);

    s_alarm=text(s,"ALM",6,5,34,16,F12,RED,LV_TEXT_ALIGN_LEFT);
    s_door=text(s,"\xE7\x9B\x96\x3A\x49\x4E\x49\x54",44,5,63,16,F12,GREEN,LV_TEXT_ALIGN_LEFT);
    s_heat=text(s,"--",110,5,44,16,F12,MUTED,LV_TEXT_ALIGN_CENTER);
    s_sv=text(s,"SV --.-",158,5,156,16,F12,MUTED,LV_TEXT_ALIGN_RIGHT);
    s_temp_title=text(s,"\xE6\xB8\xA9\xE5\xBA\xA6\x20\x54\x45\x4D\x50",8,28,184,20,F14,MUTED,LV_TEXT_ALIGN_LEFT);
    s_pv=text(s,"--.-",8,49,106,40,F32,GREEN,LV_TEXT_ALIGN_LEFT);
    text(s,"\xE2\x84\x83",118,68,24,16,F12,MUTED,LV_TEXT_ALIGN_LEFT);
    s_target=text(s,"",8,90,184,16,F12,MUTED,LV_TEXT_ALIGN_LEFT);
    text(s,"\xE5\xAD\xB5\xE8\x82\xB2\xE6\x97\xB6\xE9\x97\xB4\x20\x54\x49\x4D\x45",8,112,184,20,F14,MUTED,LV_TEXT_ALIGN_LEFT);
    s_time=text(s,"15.0",8,133,106,40,F32,0xD2ECDD,LV_TEXT_ALIGN_LEFT);
    text(s,"min",118,154,40,16,F12,MUTED,LV_TEXT_ALIGN_LEFT);
    s_phase=text(s,"",8,174,184,14,F12,MUTED,LV_TEXT_ALIGN_LEFT);
    text(s,"\xE5\xBB\xB6\xE8\xBF\x9F\xE8\xBD\xAC\x34\xE2\x84\x83\xE4\xBF\x9D\xE5\xAD\x98",8,190,140,14,F12,MUTED,LV_TEXT_ALIGN_LEFT);
    s_delay_set=text(s,"60 s",150,190,42,14,F12,MUTED,LV_TEXT_ALIGN_RIGHT);
    s_delay=text(s,"60",8,204,90,34,F28,YELLOW,LV_TEXT_ALIGN_LEFT);
    text(s,"s / N",104,218,72,16,F12,MUTED,LV_TEXT_ALIGN_LEFT);
    /* Icon bands y30..54 and y102..126 stay empty. Four explicit actions. */
    text(s,"\xE5\xBC\x80\xE5\xA7\x8B",204,58,53,20,F14,GREEN,LV_TEXT_ALIGN_CENTER);
    text(s,"SET",204,79,53,16,F12,MUTED,LV_TEXT_ALIGN_CENTER);
    text(s,"\xE5\xBC\x80\xE7\x9B\x96",263,58,54,20,F14,GREEN,LV_TEXT_ALIGN_CENTER);
    text(s,"SET",263,79,54,16,F12,MUTED,LV_TEXT_ALIGN_CENTER);
    s_mute=text(s,"\xE6\xB6\x88\xE9\x9F\xB3",204,130,53,20,F14,MUTED,LV_TEXT_ALIGN_CENTER);
    s_mute_note=text(s,"",204,151,53,16,F12,MUTED,LV_TEXT_ALIGN_CENTER);
    text(s,"\xE5\x85\xB3\xE7\x9B\x96",263,130,54,20,F14,GREEN,LV_TEXT_ALIGN_CENTER);
    text(s,"SET",263,151,54,16,F12,MUTED,LV_TEXT_ALIGN_CENTER);
    s_notice=text(s,"",205,176,110,60,F12,YELLOW,LV_TEXT_ALIGN_LEFT);
    if(!s_ok) return;
    focus_update();
    s_refresh_ms=HAL_GetTick()-100;
    ui_layout_process();
}

int ui_layout_ready(void) { return s_ok; }

void ui_layout_key(uint32_t key)
{
    if(!s_ok) return;
    if(!s_repeating && (key==KEY_UP || key==KEY_DOWN)) {
        s_repeat_key=key;
        s_hold_ms=s_repeat_ms=HAL_GetTick();
    }
    if(s_edit) {
        if(key==KEY_OK) {
            app_command_kind_t kind=s_focus_index==0?APP_SET_TEMP:s_focus_index==1?APP_SET_TIME:APP_SET_DELAY;
            if(app_service_submit(kind,s_draft)) s_edit=0;
        } else if(s_focus_index==0 && (key==KEY_LEFT || key==KEY_RIGHT)) {
            s_digit_index=(s_digit_index+(key==KEY_RIGHT?1:2))%3;
        } else if(key==KEY_UP || key==KEY_DOWN) {
            static const int32_t temp_steps[3]={1000,100,10};
            int32_t step=s_focus_index==0?temp_steps[s_digit_index]:1;
            int32_t lo=s_focus_index==0?APP_TEMP_MIN_CENTI:s_focus_index==1?APP_TIME_MIN_TENTHS:0;
            int32_t hi=s_focus_index==0?APP_TEMP_MAX_CENTI:s_focus_index==1?APP_TIME_MAX_TENTHS:APP_DELAY_MAX_S;
            s_draft+=key==KEY_UP?step:-step;
            if(s_draft<lo) s_draft=lo;
            if(s_draft>hi) s_draft=hi;
        }
    } else if(key==KEY_OK) {
        if(s_focus_index<3) {
            s_edit=1; s_digit_index=2;
            s_draft=s_focus_index==0?s_view.run.target_centi:s_focus_index==1?s_view.run.time_tenths_min:s_view.run.delay_s;
        } else {
            static const app_command_kind_t actions[4]={APP_START,APP_OPEN,APP_MUTE,APP_CLOSE};
            app_service_submit(actions[s_focus_index-3],0);
        }
    } else {
        /* Spatial navigation: parameter column and the two action columns. */
        static const uint8_t right[7]={3,5,6,4,0,6,2};
        static const uint8_t left[7] ={4,6,6,0,3,1,5};
        static const uint8_t up[7]   ={2,0,1,5,6,3,4};
        static const uint8_t down[7] ={1,2,0,5,6,3,4};
        if(key==KEY_RIGHT) s_focus_index=right[s_focus_index];
        if(key==KEY_LEFT) s_focus_index=left[s_focus_index];
        if(key==KEY_UP) s_focus_index=up[s_focus_index];
        if(key==KEY_DOWN) s_focus_index=down[s_focus_index];
    }
    focus_update();
    s_refresh_ms=HAL_GetTick()-100;
}

void ui_layout_process(void)
{
    char b[96], number[24];
    uint32_t now=HAL_GetTick(), duration, width;
    int32_t t, display_target;
    if(s_ok && s_edit && s_repeat_key<KEY_COUNT && key_is_pressed((key_id_t)s_repeat_key)) {
        if((uint32_t)(now-s_hold_ms)>=500 && (uint32_t)(now-s_repeat_ms)>=80) {
            s_repeating=1; ui_layout_key(s_repeat_key); s_repeating=0;
            s_repeat_ms=now;
        }
    } else s_repeat_key=KEY_COUNT;
    if(!s_ok || (uint32_t)(now-s_refresh_ms)<100) return;
    s_refresh_ms=now;
    app_service_snapshot(&s_view);
    display_target=s_view.temp.target_valid?s_view.temp.applied_centi:s_view.run.target_centi;
    snprintf(b,sizeof(b),"\xE7\x9B\xAE\xE6\xA0\x87\x20%ld.%ld \xE2\x84\x83",(long)(display_target/100),(long)((display_target%100)/10));
    value(s_target,b);
    snprintf(b,sizeof(b),"SV %ld.%ld%s",(long)(display_target/100),(long)((display_target%100)/10),s_view.target_pending?" ...":"");
    if(!s_view.temp.target_valid && !s_view.target_pending) strcpy(b,"SV --.-");
    value(s_sv,b);
    t=s_edit && s_focus_index==0?s_draft:s_view.temp.pv_centi;
    if(s_edit && s_focus_index==0) snprintf(number,sizeof(number),"%02ld.%ld",(long)(t/100),(long)((t%100)/10));
    else if(s_view.temp.valid) {
        int32_t tenth=t>=0?(t+5)/10:-((-t+5)/10);
        int32_t abs_t=tenth<0?-tenth:tenth;
        snprintf(number,sizeof(number),"%s%ld.%ld",tenth<0?"-":"",(long)(abs_t/10),(long)(abs_t%10));
    } else strcpy(number,"--.-");
    value(s_pv,number);
    value(s_temp_title,s_edit && s_focus_index==0?"\xE7\x9B\xAE\xE6\xA0\x87\x20\x54\x45\x4D\x50":"\xE6\xB8\xA9\xE5\xBA\xA6\x20\x54\x45\x4D\x50");
    if(s_edit && s_focus_index==0) {
        unsigned i, index=s_digit_index==2?3:s_digit_index;
        int x=8;
        lv_font_glyph_dsc_t d;
        for(i=0;i<index;i++) if(lv_font_get_glyph_dsc(F32,&d,number[i],number[i+1])) x+=d.adv_w;
        lv_obj_set_pos(s_digit,x,88);
        lv_obj_remove_flag(s_digit,LV_OBJ_FLAG_HIDDEN);
    } else lv_obj_add_flag(s_digit,LV_OBJ_FLAG_HIDDEN);

    duration=s_view.run.phase==INC_RUNNING?(s_view.run.remaining_ms+5999u)/6000u:s_view.run.time_tenths_min;
    if(s_view.run.phase>=INC_FINISHED) duration=0;
    if(s_edit && s_focus_index==1) duration=(uint32_t)s_draft;
    snprintf(b,sizeof(b),"%lu.%lu",(unsigned long)(duration/10),(unsigned long)(duration%10));
    value(s_time,b);
    switch(s_view.run.phase) {
    case INC_RUNNING: value(s_phase,"\xE5\xAD\xB5\xE8\x82\xB2 RUN"); break;
    case INC_DELAY: value(s_phase,"\xE7\xBB\x93\xE6\x9D\x9F\x20\xE5\xBB\xB6\xE8\xBF\x9F"); break;
    case INC_COOL_REQUEST: value(s_phase,"\xE8\xAE\xBE\xE5\xAE\x9A 4C ..."); break;
    case INC_COOLING: value(s_phase,"\xE9\x99\x8D\xE6\xB8\xA9 4C"); break;
    case INC_STORAGE: value(s_phase,"4C \xE4\xBF\x9D\xE5\xAD\x98"); break;
    case INC_FINISHED: value(s_phase,"\xE7\xBB\x93\xE6\x9D\x9F"); break;
    case INC_ERROR: value(s_phase,"ERROR"); break;
    default: value(s_phase,"\xE5\xBE\x85\xE5\xBC\x80\xE5\xA7\x8B"); break;
    }
    duration=s_view.run.phase==INC_DELAY?(s_view.run.delay_remaining_ms+999u)/1000u:s_view.run.delay_s;
    if(s_view.run.phase==INC_COOL_REQUEST || s_view.run.phase==INC_COOLING || s_view.run.phase==INC_STORAGE) duration=0;
    if(s_edit && s_focus_index==2) duration=(uint32_t)s_draft;
    if((s_edit && s_focus_index==2)?s_draft==0:s_view.run.delay_s==0) strcpy(b,"N");
    else snprintf(b,sizeof(b),"%lu",(unsigned long)duration);
    value(s_delay,b);
    if(s_view.run.delay_s) snprintf(b,sizeof(b),"%us",s_view.run.delay_s);
    else strcpy(b,"N");
    value(s_delay_set,b);
    width=s_view.run.phase==INC_DELAY && s_view.run.run_delay_ms?
        (uint32_t)((uint64_t)s_view.run.delay_remaining_ms*184/s_view.run.run_delay_ms):
        (s_view.run.delay_s && s_view.run.phase==INC_IDLE?184:0);
    if(width) { lv_obj_set_width(s_progress,width); lv_obj_remove_flag(s_progress,LV_OBJ_FLAG_HIDDEN); }
    else lv_obj_add_flag(s_progress,LV_OBJ_FLAG_HIDDEN);

    if(s_view.alarm) lv_obj_remove_flag(s_alarm,LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_alarm,LV_OBJ_FLAG_HIDDEN);
    if(!s_view.temp.valid || !s_view.temp.target_valid) value(s_heat,"--");
    else if(s_view.at_temperature) value(s_heat,"\xE5\x88\xB0\xE6\xB8\xA9");
    else value(s_heat,s_view.temp.pv_centi<display_target?"\xE5\x8D\x87\xE6\xB8\xA9":"\xE9\x99\x8D\xE6\xB8\xA9");
    lv_obj_set_style_text_color(s_heat,lv_color_hex(s_view.at_temperature?MUTED:RED),0);
    switch(s_view.door) {
    case MOTOR_OPEN: value(s_door,"\xE7\x9B\x96\x3A\xE5\xBC\x80"); break;
    case MOTOR_CLOSED: value(s_door,"\xE7\x9B\x96\x3A\xE5\x85\xB3"); break;
    case MOTOR_OPENING: case MOTOR_CLOSING: value(s_door,"\xE7\x9B\x96\x3A\x2E\x2E\x2E"); break;
    case MOTOR_INSTALLING: value(s_door,"\xE7\x9B\x96\x3A\x49\x4E\x49\x54"); break;
    case MOTOR_FAULT: value(s_door,"\xE7\x9B\x96\x3A\x45\x52\x52"); break;
    default: value(s_door,"\xE7\x9B\x96\x3A\x2D\x2D"); break;
    }
    lv_obj_set_style_text_color(s_door,lv_color_hex(s_view.door_fault?RED:GREEN),0);
    lv_obj_set_style_text_color(s_mute,lv_color_hex(s_view.run.buzzer?GREEN:MUTED),0);
    value(s_mute_note,s_view.run.buzzer?"SET":"--");
    if(s_edit) {
        if(s_focus_index==0) value(s_notice,s_digit_index==0?"EDIT TEMP\n10 C":s_digit_index==1?"EDIT TEMP\n1 C":"EDIT TEMP\n0.1 C");
        else value(s_notice,s_focus_index==1?"EDIT TIME\n0.1 min":"EDIT DELAY\n0 = N");
    } else {
        static const char *notices[]={"","BUSY","SET TEMP\nFIRST","CLOSE LID","TEMP OFFLINE","WRITE FAILED","DOOR ERROR","RUN LOCKED"};
        const char *msg=notices[s_view.message];
        if(s_view.target_pending) msg="SETTING...";
        else if(s_view.write_fault) msg="WRITE FAILED";
        else if(!*msg && s_view.run.buzzer && !s_view.buzzer_hardware) msg="BUZZER\nNOT WIRED";
        value(s_notice,msg);
    }
}
