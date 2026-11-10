/**
  ******************************************************************************
  * @file    bsp_buzzer.c
  * @brief   板级蜂鸣器控制 —— PD15 推挽输出
  *
  * 硬件: PD15 ----> 蜂鸣器驱动级 ----> 有源蜂鸣器
  *       高电平鸣叫, 低电平静音 (极性见 BSP_BUZZER_ON_LEVEL)
  *
  * 实现要点:
  *   1. CubeMX 把 PD15 配成了 TIM4_CH4 复用脚, 此时写 ODR 不会驱动引脚,
  *      必须先用 HAL_GPIO_Init 改成 GPIO_MODE_OUTPUT_PP。
  *   2. 先写静音电平再切输出模式, 避免重配瞬间输出不确定电平导致误鸣。
  *   3. TIM4 的 CH4 捕获走 INDIRECTTI (源为 TI3/PD14), 改成 GPIO 后
  *      不影响电机反馈的低脉宽测量。
  ******************************************************************************
  */

/* 包含文件 ------------------------------------------------------------------*/
#include "bsp_buzzer.h"

/* 私有宏定义 ----------------------------------------------------------------*/

/* 蜂鸣器控制脚: PD15 */
#define BSP_BUZZER_PORT          GPIOD
#define BSP_BUZZER_PIN           GPIO_PIN_15

/* 驱动极性: 当前硬件为高电平鸣叫。若实测为低电平有效(如 PNP/开漏驱动),
   只需把 ON/OFF 两级互换即可。 */
#define BSP_BUZZER_ON_LEVEL      GPIO_PIN_SET
#define BSP_BUZZER_OFF_LEVEL     GPIO_PIN_RESET

/* 私有变量 ------------------------------------------------------------------*/
/* 未初始化时拒绝写电平, 避免在 TIM4_CH4 复用态下做无效操作 */
static volatile uint8_t s_buzzer_inited;

/* ========================================================================== */
/* 对外接口                                                                    */
/* ========================================================================== */

/**
  * @brief  配置 PD15 为推挽输出并置静音。
  * @retval 恒为 1: GPIO 配置接口没有失败返回。
  * @note   由 app_service_init() 调用, 即 RTOS 调度器启动之前, 无并发风险。
  */
int bsp_buzzer_init(void)
{
  GPIO_InitTypeDef gpio = {0};

  __HAL_RCC_GPIOD_CLK_ENABLE();

  /* 先写静音电平, 再切换成输出模式 */
  HAL_GPIO_WritePin(BSP_BUZZER_PORT, BSP_BUZZER_PIN, BSP_BUZZER_OFF_LEVEL);

  gpio.Pin   = BSP_BUZZER_PIN;
  gpio.Mode  = GPIO_MODE_OUTPUT_PP;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(BSP_BUZZER_PORT, &gpio);

  /* 覆盖 TIM4_CH4 复用配置后再次确认静音电平 */
  HAL_GPIO_WritePin(BSP_BUZZER_PORT, BSP_BUZZER_PIN, BSP_BUZZER_OFF_LEVEL);

  s_buzzer_inited = 1;
  return 1;
}

/**
  * @brief  设置蜂鸣器电平。
  * @param  on 非 0 鸣叫, 0 静音。
  * @retval 非 0 已写入; 0 表示未初始化。
  * @note   只操作 ODR, 无阻塞, 可在中断上下文调用。
  */
int bsp_buzzer_set(int on)
{
  if (!s_buzzer_inited) return 0;

  HAL_GPIO_WritePin(BSP_BUZZER_PORT, BSP_BUZZER_PIN,
                    on ? BSP_BUZZER_ON_LEVEL : BSP_BUZZER_OFF_LEVEL);
  return 1;
}
