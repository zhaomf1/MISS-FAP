#ifndef APP_UI_H
#define APP_UI_H
#include "lvgl.h"
/* Called exclusively by uiTask; key uses the key_id_t values in key.h. */
void ui_layout_init(void);
int ui_layout_ready(void);
void ui_layout_key(uint32_t key);
void ui_layout_process(void);
#endif
