/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* 包含文件 ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* 私有包含文件 --------------------------------------------------------------*/
/* 用户代码：包含文件开始 */

/* 用户代码：包含文件结束 */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* 私有宏定义 ----------------------------------------------------------------*/

/* 用户代码：私有宏定义开始 */
/* 运行状态指示灯: PC1 = LED11, 高电平点亮(输出24V指示), 系统正常运行时闪烁 */
#define RUN_LED_GPIO_Port GPIOC
#define RUN_LED_Pin       GPIO_PIN_1
/* 用户代码：私有宏定义结束 */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
