/**
  ******************************************************************************
  * @file    bsp_board_io.h
  * @brief   新版卡(STM32F405RGT6) 板级离散 IO 接口
  *
  * 板卡定义的 12V 输出通道(均为高电平有效, 板上经驱动管放大):
  *   OUTPUT_1 = PB1  温控风扇
  *   OUTPUT_2 = PB0  蜂鸣器控制
  *   OUTPUT_3 = PC5  预留
  *   OUTPUT_4 = PC4  预留
  *
  * 引脚方向/初始电平由 gpio.c 静态配置, 本模块只负责电平操作。
  ******************************************************************************
  */
#ifndef BSP_BOARD_IO_H
#define BSP_BOARD_IO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* 12V 输出通道编号, 与板卡丝印 OUTPUT_1..4 对应 */
typedef enum {
  BSP_OUT_FAN    = 0,   /* OUTPUT_1 = PB1 温控风扇 */
  BSP_OUT_BUZZER = 1,   /* OUTPUT_2 = PB0 蜂鸣器控制 */
  BSP_OUT_AUX3   = 2,   /* OUTPUT_3 = PC5 预留 */
  BSP_OUT_AUX4   = 3,   /* OUTPUT_4 = PC4 预留 */
  BSP_OUT_COUNT
} bsp_out_ch_t;

/* 置位/复位某路 12V 输出; on!=0 输出高(打开) */
void bsp_board_output_set(bsp_out_ch_t ch, int on);

/* 蜂鸣器输出(PB0)开关, 高电平有效 */
void bsp_board_buzzer_set(int on);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_IO_H */
