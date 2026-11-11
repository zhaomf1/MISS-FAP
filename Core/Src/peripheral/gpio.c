/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */
/*
 * 新版卡 (STM32F405RGT6, LQFP64) 静态 GPIO。
 * 本板没有 PD/PE 端口, 引脚全部落在 PA/PB/PC 上。
 *
 * 输出 (推挽, 默认低):
 *   PC1  LED1_State    运行状态指示          (高电平点亮)
 *   PC2  LCD_BLK       屏背光                (高电平点亮)
 *   PC3  LCD_DC        屏 数据/命令选择
 *   PC4  OUTPUT_4      预留 12V 输出
 *   PC5  OUTPUT_3      预留 12V 输出
 *   PB0  OUTPUT_2      蜂鸣器控制 12V 输出    (高电平有效)
 *   PB1  OUTPUT_1      温控风扇 12V 输出      (高电平有效)
 *   PB9  Motor_Input   翻盖电机指令         (空闲低, 高电平脉冲)
 *   PA6  LCD_RESET     屏复位              (上电保持低=复位, lcd_init 时释放)
 *   PA4  SPI1_CS       屏片选              (软件片选, 空闲拉高)
 *   PA12 BEEP          片上蜂鸣器驱动      (固定输出低=静音; 固件不使用,
 *                                          仅防止浮空被干扰误响)
 *   PC10              未用/预留            (固定输出低, 防止浮空)
 *   PC13/PC14/PC15    未用/预留            (固定输出低, 防止浮空;
 *                                          这三个脚经 VBAT 开关供电, 输出电流
 *                                          ≤3mA 且不宜高速, 这里只用于定电平)
 *
 * 输入:
 *   PC6/PC7/PC8/PC9 PA8  按键 Right/Down/OK/Up/Left (低有效, 内部上拉)
 *   PB5/PB4/PB3/PC12/PC11  板卡 ID 拨码 Address_1(高位)..Address_5 (低电平=0)
 *   PA0/PA1            IN_1/IN_2 12V 传感器输入 (本版预留, 不做业务)
 *
 * 复用外设引脚(不在此处配置): PA5/PA7(SPI1) PB6/PB7(I2C1) PA2/PA3(USART2)
 *   PA9/PA10(USART1) PB10/PB11(USART3) PB12-PB15(SPI2) PB8(TIM4_CH3) PA13/PA14(SWD)
 */
/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins as
        * Analog
        * Input
        * Output
        * EVENT_OUT
        * EXTI
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();

  /*Configure GPIO pin Output Level : PC1 PC2 PC3 PC4 PC5
     以及未使用的 PC10/PC13/PC14/PC15: 给确定低电平, 不留浮空 */
  HAL_GPIO_WritePin(GPIOC, GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_10|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15,
                    GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level : PA4=高(未选中) PA6=低(屏保持复位)
     PA12=低: 板上"片上蜂鸣器"驱动脚, 必须给确定电平。
     不能留浮空(复位默认高阻输入), 否则温控器/12V 输出一切换就会
     通过耦合把该节点抬起来, 蜂鸣器会莫名其妙地响。 */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_4, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6|GPIO_PIN_12, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level : PB0 PB1 PB9 */
  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_9, GPIO_PIN_RESET);

  /*Configure GPIO pins : PC1 PC2 PC3 PC4 PC5 PC10 PC13 PC14 PC15 */
  GPIO_InitStruct.Pin = GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5
                          |GPIO_PIN_10|GPIO_PIN_13|GPIO_PIN_14|GPIO_PIN_15;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA4 PA6 PA12 */
  GPIO_InitStruct.Pin = GPIO_PIN_4|GPIO_PIN_6|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB0 PB1 PB9 */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PC6 PC7 PC8 PC9 (按键, 低有效需上拉) */
  GPIO_InitStruct.Pin = GPIO_PIN_6|GPIO_PIN_7|GPIO_PIN_8|GPIO_PIN_9;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : PA8 (KEY_Left, 低有效需上拉) */
  GPIO_InitStruct.Pin = GPIO_PIN_8;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pins : PB3 PB4 PB5 (板卡ID Address_3/2/1, 拨码合上为高) */
  GPIO_InitStruct.Pin = GPIO_PIN_3|GPIO_PIN_4|GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /*Configure GPIO pins : PC11 PC12 (板卡ID Address_5/4, 拨码合上为高) */
  GPIO_InitStruct.Pin = GPIO_PIN_11|GPIO_PIN_12;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pins : PA0 PA1 (IN_1 / IN_2, 12V 传感器输入, 本版预留) */
  GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLDOWN;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
