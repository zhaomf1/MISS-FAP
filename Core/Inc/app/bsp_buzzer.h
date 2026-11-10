/**
  ******************************************************************************
  * @file    bsp_buzzer.h
  * @brief   板级蜂鸣器控制 —— PD15 推挽输出 (有源蜂鸣器, 高电平鸣叫)
  *
  * 硬件连接:
  *   PD15 ----> 蜂鸣器驱动级 ----> 有源蜂鸣器
  *   有效电平由 bsp_buzzer.c 的 BSP_BUZZER_ON_LEVEL 决定。
  *
  * 引脚复用说明:
  *   .ioc / tim.c 里 PD15 被 CubeMX 默认分配为 TIM4_CH4 (AF2)。
  *   本工程 TIM4 只有 CH3(PD14) 用作输入捕获, CH4 配置成 INDIRECTTI
  *   (捕获源仍是 TI3), 所以 PD15 的复用功能实际不参与任何逻辑,
  *   可以安全改作 GPIO 输出。bsp_buzzer_init() 在运行时重配 PD15,
  *   因此 CubeMX 重新生成 tim.c/gpio.c 后也不必改 .ioc 就能正常鸣叫。
  ******************************************************************************
  */
#ifndef BSP_BUZZER_H
#define BSP_BUZZER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/**
  * @brief  配置蜂鸣器控制脚 (PD15) 为推挽输出, 并置为静音。
  * @return 非 0 表示配置完成; 0 表示硬件不可用。
  * @note   必须在 RTOS 调度器启动前、且在 MX_TIM4_Init 之后调用,
  *         以便覆盖 CubeMX 给 PD15 的 TIM4_CH4 复用配置。
  */
int bsp_buzzer_init(void);

/**
  * @brief  设置蜂鸣器鸣叫或静音。
  * @param  on 非 0 鸣叫, 0 静音。
  * @return 非 0 表示电平已写入; 0 表示尚未调用 bsp_buzzer_init()。
  * @note   只写 ODR, 不做延时, 可在任务或中断上下文调用。
  */
int bsp_buzzer_set(int on);

#ifdef __cplusplus
}
#endif
#endif /* BSP_BUZZER_H */
