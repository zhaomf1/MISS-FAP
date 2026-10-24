/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "motor.h"
#include "iwdg.h"
#include "lvgl_port.h"
#include "key.h"
#include "modbus_rtu.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
static const osThreadAttr_t motorTask_attributes = {
  .name = "motorTask",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal,
};
static const osThreadAttr_t uiTask_attributes = {
  .name = "uiTask",
  .stack_size = 2048 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
static const osThreadAttr_t keyTask_attributes = {
  .name = "keyTask",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* USER CODE END Variables */
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* RTOS synchronization objects must be created after osKernelInitialize(). */
  if (modbus_rtu_init() != MODBUS_OK) {
    Error_Handler();
  }

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* 电机驱动服务任务：只提供底层控制和状态反馈，不自动执行动作。 */
  osThreadNew(MotorTask, NULL, &motorTask_attributes);
  osThreadNew(lvgl_port_task, NULL, &uiTask_attributes);
  osThreadNew(key_task, NULL, &keyTask_attributes);
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  uint32_t blink = 0;   /* 运行指示灯计时, ms */
  for(;;)
  {
    HAL_IWDG_Refresh(&hiwdg);   /* IWDG超时约0.5s, 全工程原本无喂狗 */
    osDelay(100);
    blink += 100;
    if (blink >= 500)           /* 0.5s翻转一次: 亮0.5s灭0.5s; 改250=每0.5s一个亮灭周期 */
    {
      blink -= 500;
      HAL_GPIO_TogglePin(RUN_LED_GPIO_Port, RUN_LED_Pin);   /* PC1=LED11 运行状态灯 */
    }
  }
  /* USER CODE END StartDefaultTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* Stack overflow diagnostics, readable from the debugger after the halt.
 * g_stack_overflow_task points to the task's static name string. */
volatile uint32_t g_stack_overflow_count;
const char * volatile g_stack_overflow_task;

/**
  * @brief  Called by FreeRTOS when configCHECK_FOR_STACK_OVERFLOW detects that
  *         a task wrote into the last 20 bytes of its stack.
  * @note   Do NOT call printf or any other stack-hungry function here: the
  *         offending task's stack is already corrupted. Interrupts stay
  *         disabled, so the scheduler stops and the debugger can inspect the
  *         two globals above. The independent watchdog (IWDG, ~0.5 s) then
  *         resets the MCU, which is the intended recovery for an unattended
  *         device.
  */
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void)xTask;

  g_stack_overflow_task = pcTaskName;
  g_stack_overflow_count++;

  taskDISABLE_INTERRUPTS();
  for(;;)
  {
  }
}

/* USER CODE END Application */

