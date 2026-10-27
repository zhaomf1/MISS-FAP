/**
  ******************************************************************************
  * @file    motor.c
  * @brief   SND2506 翻盖电机驱动 (纯驱动版, 从调试电机分支移植 2026-10-19)
  *          TIM4_CH3(PD14) 输入捕获测反馈 + PD13 GPIO 发脉宽指令
  *          + FreeRTOS 状态机服务任务
  *
  * 移植说明: 本文件为 master 分支驱动版, 只包含驱动本体——
  *   硬件捕获/指令发送/状态机/超时保护/应用接口。
  * 不包含(留在调试分支): CLI调试命令口, 自动老化测试状态机,
  *   反馈环形缓存dump, 重试/退避/断电自愈(应用层策略, 需要时按
  *   调试分支 40~44 节实现移植)。
  *
  * 若 CubeMX 重新生成代码, 本模块运行时自行重配 PD13/PD14/TIM4,
  * 无需依赖 .ioc; 但 main.c 的 PD13 早期拉低保护需保留(见 main.c)。
  ******************************************************************************
  */

/* 包含文件 ------------------------------------------------------------------*/
#include "motor.h"
#include <stdio.h>
#include "cmsis_os.h"
#include "tim.h"      /* htim4 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* 私有宏定义 ----------------------------------------------------------------*/

/* 指令输出脚 */
#define MOTOR_CMD_PORT           GPIOD
#define MOTOR_CMD_PIN            GPIO_PIN_13

/* 反馈输入脚 (TIM4_CH3, AF2) */
#define MOTOR_FB_PORT            GPIOD
#define MOTOR_FB_PIN             GPIO_PIN_14

/* TIM4 计数时钟: 84MHz / (83+1) = 1MHz -> 1us 分辨率, 16bit 量程 65.5ms */
#define MOTOR_TIM_PRESCALER      83

#if MOTOR_CMD_INVERTED
/* 开漏经2N7002反相. 空闲=输出高(MOS导通, Input=0V), 脉冲=拉低 */
#define CMD_IDLE_LEVEL           GPIO_PIN_SET
#define CMD_ACTIVE_LEVEL         GPIO_PIN_RESET
#define CMD_GPIO_MODE            GPIO_MODE_OUTPUT_OD
#else
/* 推挽3.3V直推. 空闲=输出低, 脉冲=输出高 */
#define CMD_IDLE_LEVEL           GPIO_PIN_RESET
#define CMD_ACTIVE_LEVEL         GPIO_PIN_SET
#define CMD_GPIO_MODE            GPIO_MODE_OUTPUT_PP
#endif

/* 反馈解码事件: 驱动只消费两个到位心跳 */
typedef enum {
  FB_NONE = 0,
  FB_OPEN_DONE,       /* 0.8ms  开到位心跳 */
  FB_CLOSED_DONE,     /* 7.5ms  关到位心跳 */
  FB_OTHER,           /* 其余(过程/减速码), 忽略 */
} fb_event_t;

/* 服务任务命令 */
typedef enum { MCMD_OPEN = 0, MCMD_CLOSE, MCMD_REINIT } mcmd_type_t;

typedef struct {
  mcmd_type_t type;
  uint8_t     speed;   /* 1~3 档 */
} motor_cmd_t;

/* 私有变量 ------------------------------------------------------------------*/

static QueueHandle_t     s_cmd_q;      /* motor_cmd_t, 深度1: 忙时直接拒收 */
static QueueHandle_t     s_fb_q;       /* uint32_t 低脉宽(us), ISR->task */

static volatile motor_state_t s_state = MOTOR_UNINIT;
static volatile uint32_t s_feedback_ms;
static volatile uint8_t s_feedback_seen;
static volatile uint8_t s_command_pending;
static volatile TickType_t   s_deadline = 0;     /* 当前阶段超时时刻 */
static volatile TickType_t   s_move_start = 0;   /* 本次动作发起时刻 */

static volatile uint32_t    s_cap_fall = 0;      /* ISR: 最近下降沿(低脉冲开始) */
static volatile uint8_t     s_power_on = 0;

/* 私有函数声明 --------------------------------------------------------------*/
static void motor_hw_init(void);
static void motor_do_install(void);
static void motor_act_open(uint8_t speed);
static void motor_act_close(uint8_t speed);
static void motor_pulse(uint16_t width_ms);
static fb_event_t fb_decode(uint32_t w_us);
static void fb_accept(fb_event_t e);
static void set_state(motor_state_t st);

/* ========================================================================== */
/* 硬件层                                                                      */
/* ========================================================================== */

/**
  * @brief  运行时重配 PD13/PD14/TIM4 (兼容 CubeMX 重新生成代码)
  * @note   MX_TIM4_Init 已把 TIM4 时钟打开, 这里只改分频/通道/引脚
  */
static void motor_hw_init(void)
{
  GPIO_InitTypeDef gpio = {0};
  TIM_IC_InitTypeDef ic = {0};

  /* --- PD13: 指令输出. 先写空闲电平再切输出, 防止上电毛刺 --- */
  HAL_GPIO_WritePin(MOTOR_CMD_PORT, MOTOR_CMD_PIN, CMD_IDLE_LEVEL);
  gpio.Pin   = MOTOR_CMD_PIN;
  gpio.Mode  = CMD_GPIO_MODE;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MOTOR_CMD_PORT, &gpio);

  /* --- PD14: TIM4_CH3 输入捕获 (AF2) --- */
  gpio.Pin       = MOTOR_FB_PIN;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = MOTOR_FB_PULLUP ? GPIO_PULLUP : GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF2_TIM4;
  HAL_GPIO_Init(MOTOR_FB_PORT, &gpio);

  /* --- TIM4: 1MHz 计数 --- */
  htim4.Init.Prescaler = MOTOR_TIM_PRESCALER;
  htim4.Init.Period    = 0xFFFFu;
  HAL_TIM_Base_Init(&htim4);

  /* 反馈空闲高+低脉冲(实测, 与文档一致)
   * CH3: 下降沿(低脉冲开始), IC3 经 DIRECTTI  映射到 TI3 (即 PD14)
   * CH4: 上升沿(低脉冲结束), IC4 经 INDIRECTTI 映射到 TI3 (不占用PD15) */
  ic.ICPrescaler = TIM_ICPSC_DIV1;
  ic.ICFilter    = 0x0F;                    /* 滤除 <3us 毛刺 */
  ic.ICPolarity  = TIM_ICPOLARITY_FALLING;
  ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
  HAL_TIM_IC_ConfigChannel(&htim4, &ic, TIM_CHANNEL_3);
  ic.ICPolarity  = TIM_ICPOLARITY_RISING;
  ic.ICSelection = TIM_ICSELECTION_INDIRECTTI;
  HAL_TIM_IC_ConfigChannel(&htim4, &ic, TIM_CHANNEL_4);

  HAL_NVIC_SetPriority(TIM4_IRQn, 6, 0);    /* 低于 FreeRTOS 系统调用上限(5) */
  HAL_NVIC_EnableIRQ(TIM4_IRQn);
  HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_3);
  HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_4);
  motor_power_set(1);
}

/**
  * @brief  发一个脉宽指令 (空闲->有效->延时 width_ms -> 空闲)
  * @note   在 MotorTask 上下文调用; 容差±3ms, osDelay(1kHz) 足够
  */
static void motor_pulse(uint16_t width_ms)
{
  HAL_GPIO_WritePin(MOTOR_CMD_PORT, MOTOR_CMD_PIN, CMD_ACTIVE_LEVEL);
  osDelay(width_ms);
  HAL_GPIO_WritePin(MOTOR_CMD_PORT, MOTOR_CMD_PIN, CMD_IDLE_LEVEL);
  printf("[motor] pulse %ums sent\r\n", width_ms);
}

/* ========================================================================== */
/* 中断服务 (TIM4 捕获)                                                         */
/* ========================================================================== */

void TIM4_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim4);
}

/**
  * @brief  TIM 输入捕获回调: CH3记下降沿(低脉冲开始), CH4记上升沿(结束)
  * @note   到位信号空闲高+低脉冲, 低脉宽即状态码。ISR 只算宽度入队。
  */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  BaseType_t hprio_woken = pdFALSE;

  if (htim->Instance != TIM4)
    return;

  if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_3)          /* 下降沿: 低脉冲开始 */
  {
    s_cap_fall = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_3);
  }
  else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_4)    /* 上升沿: 低脉冲结束 */
  {
    uint32_t rise = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_4);
    uint32_t fall = s_cap_fall;
    uint32_t w_us;

    /* 低脉冲宽度 (16bit 回绕处理) */
    w_us = (rise >= fall) ? (rise - fall) : (rise + 0x10000u - fall);
    xQueueSendFromISR(s_fb_q, &w_us, &hprio_woken);
  }
  portYIELD_FROM_ISR(hprio_woken);
}

/* ========================================================================== */
/* 反馈解码 + 状态机                                                            */
/* ========================================================================== */

/**
  * @brief  低脉宽解码: 只识别两个到位心跳, 其余返回 FB_OTHER 忽略
  */
static fb_event_t fb_decode(uint32_t w_us)
{
  int32_t d;

  d = (int32_t)w_us - MOTOR_FB_OPEN_DONE_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_OPEN_DONE;
  d = (int32_t)w_us - MOTOR_FB_CLOSED_DONE_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_CLOSED_DONE;
  return FB_OTHER;
}

static void set_state(motor_state_t st)
{
  if (st != s_state)
  {
    s_state = st;
    printf("[motor] state -> %s\r\n", motor_state_str(st));
  }
}

/**
  * @brief  状态机: 只消费到位心跳 (2026-10-19 简化方案)
  * @note   到位心跳是持续广播的; 电机接受指令并运动后线上变为过程码,
  *         心跳消失。因此"旧位置心跳持续"= 电机没理会指令(忙窗口)。
  */
static void fb_accept(fb_event_t e)
{
  if(e == FB_OPEN_DONE || e == FB_CLOSED_DONE) {
    s_feedback_ms = HAL_GetTick();
    s_feedback_seen = 1;
  }
  switch (s_state)
  {
    case MOTOR_INSTALLING:
      /* 校准结束后停在哪个到位位置就同步哪个。
       * i90 后至少 INSTALL_MIN_MS 才认: 校准期间趴在关位的 7.5 心跳
       * 不代表校准完成, 提前认会导致后续指令撞校准窗口被忽略 */
      if ((xTaskGetTickCount() - s_move_start) >=
          pdMS_TO_TICKS(MOTOR_INSTALL_MIN_MS))
      {
        if (e == FB_CLOSED_DONE)      set_state(MOTOR_CLOSED);
        else if (e == FB_OPEN_DONE)   set_state(MOTOR_OPEN);
      }
      break;

    case MOTOR_OPENING:
      if (e == FB_OPEN_DONE) set_state(MOTOR_OPEN);
      /* 指令后仍持续收到旧位置(关位)心跳 = 电机没理会指令(忙窗口) */
      else if (e == FB_CLOSED_DONE &&
               (xTaskGetTickCount() - s_move_start) >=
               pdMS_TO_TICKS(MOTOR_FB_FIRST_TIMEOUT_MS))
      {
        printf("[motor] FAULT: motor busy (still at closed, ignored cmd)\r\n");
        set_state(MOTOR_FAULT);
      }
      break;

    case MOTOR_CLOSING:
      if (e == FB_CLOSED_DONE) set_state(MOTOR_CLOSED);
      else if (e == FB_OPEN_DONE &&
               (xTaskGetTickCount() - s_move_start) >=
               pdMS_TO_TICKS(MOTOR_FB_FIRST_TIMEOUT_MS))
      {
        printf("[motor] FAULT: motor busy (still at open, ignored cmd)\r\n");
        set_state(MOTOR_FAULT);
      }
      break;

    case MOTOR_OPEN:
      /* 堵转自恢复/手动移盖: 状态自动同步 */
      if (e == FB_CLOSED_DONE) set_state(MOTOR_CLOSED);
      break;

    case MOTOR_CLOSED:
      if (e == FB_OPEN_DONE) set_state(MOTOR_OPEN);
      break;

    default:
      break;
  }
}

/**
  * @brief  安装方式初始化: 每次电机上电后必须执行一次
  *         (motor_init() 触发; 电机自校准会转动一次)
  */
static void motor_do_install(void)
{
  uint16_t pulse_ms = (MOTOR_SIDE == MOTOR_SIDE_LEFT)
                      ? MOTOR_PULSE_INSTALL_LEFT : MOTOR_PULSE_INSTALL_RIGHT;

  printf("[motor] install %s, sending %ums pulse...\r\n",
         (MOTOR_SIDE == MOTOR_SIDE_LEFT) ? "LEFT" : "RIGHT", pulse_ms);
  motor_pulse(pulse_ms);
  set_state(MOTOR_INSTALLING);
  s_move_start = xTaskGetTickCount();
  s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(MOTOR_INSTALL_TIMEOUT_MS);
}

/**
  * @brief  执行上翻动作: 发上升指令并进入 OPENING 等待
  */
static void motor_act_open(uint8_t speed)
{
  motor_pulse(MOTOR_PULSE_UP_1 + (speed - 1) * 10);   /* 10/20/30 */
  set_state(MOTOR_OPENING);
  s_move_start = xTaskGetTickCount();
  s_deadline = s_move_start + pdMS_TO_TICKS(MOTOR_MOVE_TIMEOUT_MS);
}

/**
  * @brief  执行下翻动作: 发下降指令并进入 CLOSING 等待
  */
static void motor_act_close(uint8_t speed)
{
  motor_pulse(MOTOR_PULSE_DOWN_1 + (speed - 1) * 10); /* 50/60/70 */
  set_state(MOTOR_CLOSING);
  s_move_start = xTaskGetTickCount();
  s_deadline = s_move_start + pdMS_TO_TICKS(MOTOR_MOVE_TIMEOUT_MS);
}

/* ========================================================================== */
/* 服务任务                                                                     */
/* ========================================================================== */

void MotorTask(void *argument)
{
  (void)argument;
  motor_cmd_t cmd;
  uint32_t w_us;

  s_cmd_q = xQueueCreate(1, sizeof(motor_cmd_t));
  s_fb_q  = xQueueCreate(16, sizeof(uint32_t));
  configASSERT(s_cmd_q && s_fb_q);

  motor_hw_init();
  printf("[motor] hw init done, waiting motor boot %ums...\r\n",
         MOTOR_STARTUP_DELAY_MS);
  osDelay(MOTOR_STARTUP_DELAY_MS);

  /* 驱动版: 上电不自动安装。应用层按业务时机调用 motor_init()。
   * 电机每次上电必须先收到安装指令才能正常升降。 */

  for (;;)
  {
    TickType_t now;

    /* --- 1. 处理命令 (深度1队列, 最多等50ms) --- */
    if (xQueueReceive(s_cmd_q, &cmd, pdMS_TO_TICKS(50)) == pdPASS)
    {
      switch (cmd.type)
      {
        case MCMD_OPEN:
          motor_act_open(cmd.speed);
          break;

        case MCMD_CLOSE:
          motor_act_close(cmd.speed);
          break;

        case MCMD_REINIT:
          motor_do_install();
          break;
      }
      s_command_pending = 0;
    }

    /* --- 2. 处理反馈 (非阻塞排空) --- */
    while (xQueueReceive(s_fb_q, &w_us, 0) == pdPASS)
    {
      fb_accept(fb_decode(w_us));
    }

    /* --- 3. 超时检查 --- */
    now = xTaskGetTickCount();
    switch (s_state)
    {
      case MOTOR_INSTALLING:
        if ((int32_t)(now - s_deadline) >= 0)
        {
          printf("[motor] FAULT: install no response (check 12V/wiring/fb)\r\n");
          set_state(MOTOR_FAULT);
        }
        break;

      case MOTOR_OPENING:
      case MOTOR_CLOSING:
        /* 心跳消失后 20s 内必须出现目标到位心跳, 否则真故障 */
        if ((int32_t)(now - s_deadline) >= 0)
        {
          printf("[motor] FAULT: move timeout\r\n");
          set_state(MOTOR_FAULT);
        }
        break;

      default:
        break;   /* 静止态靠心跳自动同步, 不设超时 */
    }
  }
}

/* ========================================================================== */
/* 应用接口                                                                     */
/* ========================================================================== */

motor_state_t motor_get_state(void)
{
  return s_state;
}

uint32_t motor_feedback_age_ms(void)
{
  return s_feedback_seen ? HAL_GetTick() - s_feedback_ms : UINT32_MAX;
}

int motor_command_pending(void) { return s_command_pending != 0; }

const char *motor_state_str(motor_state_t s)
{
  switch (s)
  {
    case MOTOR_UNINIT:     return "UNINIT";
    case MOTOR_INSTALLING: return "INSTALLING";
    case MOTOR_OPENING:    return "OPENING";
    case MOTOR_OPEN:       return "OPEN";
    case MOTOR_CLOSING:    return "CLOSING";
    case MOTOR_CLOSED:     return "CLOSED";
    case MOTOR_FAULT:      return "FAULT";
    default:               return "?";
  }
}

static motor_ret_t send_cmd(mcmd_type_t type, uint8_t speed)
{
  motor_cmd_t cmd = { type, speed };
  motor_state_t st = s_state;

  if (s_cmd_q == NULL)
    return MOTOR_RET_NOT_INIT;
  if(s_command_pending) return MOTOR_RET_BUSY;

  /* 命令门禁: 安装/运动中拒绝, FAULT 须先 motor_init() 恢复 */
  if (st == MOTOR_UNINIT || st == MOTOR_INSTALLING) return MOTOR_RET_NOT_INIT;
  if (st == MOTOR_OPENING || st == MOTOR_CLOSING)    return MOTOR_RET_BUSY;
  if (st == MOTOR_FAULT)                             return MOTOR_RET_FAULT;

  s_command_pending = 1;
  if (xQueueSend(s_cmd_q, &cmd, 0) != pdPASS) {
    s_command_pending = 0;
    return MOTOR_RET_BUSY;
  }
  return MOTOR_RET_OK;
}

motor_ret_t motor_init(void)
{
  motor_cmd_t cmd = { MCMD_REINIT, 0 };

  if (s_cmd_q == NULL)
    return MOTOR_RET_NOT_INIT;
  if(s_command_pending) return MOTOR_RET_BUSY;

  /* 安装指令任何时候都允许(含 FAULT 恢复); 仅安装中拒收 */
  if (s_state == MOTOR_INSTALLING)
    return MOTOR_RET_BUSY;

  s_command_pending = 1;
  if (xQueueSend(s_cmd_q, &cmd, 0) != pdPASS) {
    s_command_pending = 0;
    return MOTOR_RET_BUSY;
  }
  return MOTOR_RET_OK;
}

motor_ret_t motor_open(motor_speed_t speed)
{
  if (speed < MOTOR_SPEED_1) speed = MOTOR_SPEED_2;
  if (speed > MOTOR_SPEED_3) speed = MOTOR_SPEED_2;
  return send_cmd(MCMD_OPEN, (uint8_t)speed);
}

motor_ret_t motor_close(motor_speed_t speed)
{
  if (speed < MOTOR_SPEED_1) speed = MOTOR_SPEED_2;
  if (speed > MOTOR_SPEED_3) speed = MOTOR_SPEED_2;
  return send_cmd(MCMD_CLOSE, (uint8_t)speed);
}

void motor_power_set(int on)
{
  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6,
                    on ? GPIO_PIN_SET : GPIO_PIN_RESET);
  s_power_on = on ? 1u : 0u;
}

int motor_power_get(void)
{
  return s_power_on ? 1 : 0;
}
