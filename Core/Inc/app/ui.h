/**
  ******************************************************************************
  * @file    ui.h
  * @brief   主界面 UI (240x320): 系统状态栏 + 温度 + 时间 + 开/关舱门按钮
  *
  * 演示版(2026-10-20): UiTask 上电自动绘制整屏, 焦点框每 800ms 自动轮巡
  *   四个功能区域, 时间为走秒的定时任务(非真实时钟)。按键/电机暂未接入。
  * 正式版: 焦点由左右键驱动、确认进入编辑、上下调值 —— 按本文件的
  *   ui_draw_text16/ ui_draw_big32/ ui_frame 接口扩展即可。
  ******************************************************************************
  */
#ifndef UI_H
#define UI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 由 freertos.c 的 MX_FREERTOS_Init 创建 */
void UiTask(void *argument);

#ifdef __cplusplus
}
#endif
#endif /* UI_H */
