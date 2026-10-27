#ifndef APP_UI_H
#define APP_UI_H
#include "lvgl.h"
/**
 * @brief 创建 LVGL 页面并初始化界面状态。
 * @note  必须在 LVGL 初始化完成后由 uiTask 调用一次。
 */
void ui_layout_init(void);

/**
 * @brief 返回页面控件是否创建成功。
 * @return 非 0 表示界面可以刷新并接收按键事件。
 */
int ui_layout_ready(void);

/**
 * @brief 使用 key.h 中的 key_id_t 值处理一次按键事件。
 * @param key 由 uiTask 传入的按键编码。
 */
void ui_layout_key(uint32_t key);

/**
 * @brief 根据应用状态快照刷新页面。
 * @note  由 uiTask 周期调用，不执行阻塞式硬件 I/O。
 */
void ui_layout_process(void);
#endif
