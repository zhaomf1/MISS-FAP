/**
  ******************************************************************************
  * @file    bsp_board_io.c
  * @brief   新版卡板级离散 IO 实现 (12V 输出 / 蜂鸣器)
  *
  * 只做 GPIO 电平操作, 不含任何业务逻辑。
  ******************************************************************************
  */

#include "bsp_board_io.h"

typedef struct {
  GPIO_TypeDef *port;
  uint16_t      pin;
} bsp_out_pin_t;

/* 通道到引脚的映射, 顺序必须与 bsp_out_ch_t 一致 */
static const bsp_out_pin_t s_out_pins[BSP_OUT_COUNT] = {
  {GPIOB, GPIO_PIN_1},   /* OUTPUT_1 温控风扇 */
  {GPIOB, GPIO_PIN_0},   /* OUTPUT_2 蜂鸣器控制 */
  {GPIOC, GPIO_PIN_5},   /* OUTPUT_3 预留 */
  {GPIOC, GPIO_PIN_4},   /* OUTPUT_4 预留 */
};

void bsp_board_output_set(bsp_out_ch_t ch, int on)
{
  if ((unsigned)ch >= (unsigned)BSP_OUT_COUNT)
    return;

  HAL_GPIO_WritePin(s_out_pins[ch].port, s_out_pins[ch].pin,
                    on ? GPIO_PIN_SET : GPIO_PIN_RESET);   /* 高电平有效 */
}

/* 回读引脚实际电平: 推挽输出时 IDR 反映的是引脚真实状态,
 * 用于排查"代码认为已打开但引脚仍为低"的情况。 */
int bsp_board_output_get(bsp_out_ch_t ch)
{
  if ((unsigned)ch >= (unsigned)BSP_OUT_COUNT)
    return 0;

  return HAL_GPIO_ReadPin(s_out_pins[ch].port, s_out_pins[ch].pin) == GPIO_PIN_SET;
}

void bsp_board_buzzer_set(int on)
{
  bsp_board_output_set(BSP_OUT_BUZZER, on);
}

/* 覆盖 app_service.c 中的 __WEAK 空实现, 让应用层蜂鸣状态真正落到 PB0。
 * 返回值语义与弱实现一致: 非 0 表示硬件已生效。 */
int app_board_buzzer_set(int enabled)
{
  bsp_board_buzzer_set(enabled != 0);
  return 1;
}
