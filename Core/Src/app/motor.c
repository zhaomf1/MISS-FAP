/**
  ******************************************************************************
  * @file    motor.c
  * @brief   SND2506 翻盖电机驱动 (纯驱动版, 从调试电机分支移植 2026-10-19)
  *          TIM4_CH3(PB8) 输入捕获测反馈 + PB9 GPIO 发脉宽指令
  *          + FreeRTOS 状态机服务任务
  *
  * 移植说明: 本文件为 master 分支驱动版, 只包含驱动本体——
  *   硬件捕获/指令发送/状态机/超时保护/应用接口。
  * 不包含(留在调试分支): CLI调试命令口, 自动老化测试状态机,
  *   反馈环形缓存dump, 重试/退避/断电自愈(应用层策略, 需要时按
  *   调试分支 40~44 节实现移植)。
  *
  * 若 CubeMX 重新生成代码, 本模块运行时自行重配 PB9/PB8/TIM4,
  * 无需依赖 .ioc; 电机 12V 在新版卡上常通供电, 不再有电源开关脚。
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

/* 指令输出脚 (新版卡: PB9 = Motor_Input) */
#define MOTOR_CMD_PORT           GPIOB
#define MOTOR_CMD_PIN            GPIO_PIN_9

/* 反馈输入脚 (新版卡: PB8 = Motor_Feedback, TIM4_CH3/AF2) */
#define MOTOR_FB_PORT            GPIOB
#define MOTOR_FB_PIN             GPIO_PIN_8

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
  FB_UP_MOVING,       /* 2.5ms  上翻过程反馈 */
  FB_UP_DECEL,        /* 3.3ms  上翻减速反馈 */
  FB_DOWN_DECEL,      /* 5.0ms  下翻减速反馈 */
  FB_DOWN_MOVING,     /* 5.8ms  下翻过程反馈 */
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

static volatile uint32_t    s_cap_fall = 0;      /* ISR: 最近下降沿(低脉冲开始) */
static TickType_t s_last_fb_rx_tick;
static uint8_t s_fb_rx_seen;

/* 私有函数声明 --------------------------------------------------------------*/
static void motor_hw_init(void);
static void motor_do_install(void);
static void motor_act_open(uint8_t speed);
static void motor_act_close(uint8_t speed);
static void motor_pulse(uint16_t width_ms);
static void motor_send_command_pulses(uint16_t pulse_ms);
static fb_event_t fb_decode(uint32_t w_us);
static void fb_accept(fb_event_t e);
static void set_state(motor_state_t st);

/* ========================================================================== */
/* 硬件层                                                                      */
/* ========================================================================== */

/**
  * @brief  运行时重配 PB9/PB8/TIM4
  * @note   MX_TIM4_Init 已把 TIM4 时钟打开, 这里只改分频/通道/引脚
  */
static void motor_hw_init(void)
{
  GPIO_InitTypeDef gpio = {0};
  TIM_IC_InitTypeDef ic = {0};

  /* --- PB9: 指令输出. 先写空闲电平再切输出, 防止上电毛刺 --- */
  HAL_GPIO_WritePin(MOTOR_CMD_PORT, MOTOR_CMD_PIN, CMD_IDLE_LEVEL);
  gpio.Pin   = MOTOR_CMD_PIN;
  gpio.Mode  = CMD_GPIO_MODE;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MOTOR_CMD_PORT, &gpio);

  /* --- PB8: TIM4_CH3 输入捕获 (AF2) --- */
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
   * CH3: 下降沿(低脉冲开始), IC3 经 DIRECTTI  映射到 TI3 (即 PB8)
   * CH4: 上升沿(低脉冲结束), IC4 经 INDIRECTTI 映射到 TI3 (不占用 PB9) */
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

/* 开/关动作各发送两次相同命令脉冲，中间留出电机识别间隔。 */
static void motor_send_command_pulses(uint16_t pulse_ms)
{
  motor_pulse(pulse_ms);
  osDelay(MOTOR_COMMAND_REPEAT_DELAY_MS);
  printf("[motor] resend command pulse after %ums\r\n",
         MOTOR_COMMAND_REPEAT_DELAY_MS);
  // motor_pulse(pulse_ms);
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
  d = (int32_t)w_us - MOTOR_FB_MOVING_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_UP_MOVING;
  d = (int32_t)w_us - MOTOR_FB_UP_DECEL_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_UP_DECEL;
  d = (int32_t)w_us - MOTOR_FB_DOWN_DECEL_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_DOWN_DECEL;
  d = (int32_t)w_us - MOTOR_FB_DOWN_MOVING_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_DOWN_MOVING;
  d = (int32_t)w_us - MOTOR_FB_CLOSED_DONE_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_CLOSED_DONE;
  return FB_OTHER;
}

/* 将反馈类别转成短标签，便于串口日志直接看出电机处于哪个阶段。 */
static const char *fb_event_str(fb_event_t e)
{
  switch(e) {
    case FB_OPEN_DONE:   return "OPEN_DONE";
    case FB_UP_MOVING:   return "UP_MOVING";
    case FB_UP_DECEL:    return "UP_DECEL";
    case FB_DOWN_DECEL:  return "DOWN_DECEL";
    case FB_DOWN_MOVING: return "DOWN_MOVING";
    case FB_CLOSED_DONE: return "CLOSED_DONE";
    case FB_OTHER:       return "OTHER";
    default:             return "NONE";
  }
}

static void set_state(motor_state_t st)
{
  if (st != s_state)
  {
    printf("[motor] state %s -> %s\r\n", motor_state_str(s_state), motor_state_str(st));
    s_state = st;
  }
}

/**
  * @brief  纯位置跟踪: 到位心跳 -> OPEN/CLOSED, 过程码 -> OPENING/CLOSING
  * @note   这里不做任何异常判断。电机是否理会指令、是否超时、是否堵转,
  *         全部由电机自身与机械结构决定; 固件只反映反馈线上报告的位置。
  */
static void fb_accept(fb_event_t e)
{
  if(e == FB_OPEN_DONE || e == FB_CLOSED_DONE) {
    s_feedback_ms = HAL_GetTick();
    s_feedback_seen = 1;
    set_state(e == FB_OPEN_DONE ? MOTOR_OPEN : MOTOR_CLOSED);
  }
  else if(e == FB_UP_MOVING || e == FB_UP_DECEL) {
    /* 过程码: 无论指令来源(按键/手动推盖), 一律显示"正在开" */
    if(s_state != MOTOR_OPENING) set_state(MOTOR_OPENING);
  }
  else if(e == FB_DOWN_MOVING || e == FB_DOWN_DECEL) {
    if(s_state != MOTOR_CLOSING) set_state(MOTOR_CLOSING);
  }
  /* FB_OTHER / FB_NONE: 与开合无关的码, 忽略 */
}

/**
  * @brief  安装方式初始化: 每次电机上电后必须执行一次
  *         (motor_init() 触发; 电机自校准会转动一次)
  * @note   只发指令, 不等待、不判超时; 随后的到位心跳会把状态同步到 OPEN/CLOSED
  */
static void motor_do_install(void)
{
  uint16_t pulse_ms = (MOTOR_SIDE == MOTOR_SIDE_LEFT)
                      ? MOTOR_PULSE_INSTALL_LEFT : MOTOR_PULSE_INSTALL_RIGHT;

  printf("[motor] install %s, sending %ums pulse...\r\n",
         (MOTOR_SIDE == MOTOR_SIDE_LEFT) ? "LEFT" : "RIGHT", pulse_ms);
  motor_pulse(pulse_ms);
  set_state(MOTOR_INSTALLING);
}

/**
  * @brief  执行上翻动作: 发上升指令
  */
static void motor_act_open(uint8_t speed)
{
  uint16_t pulse_ms = MOTOR_PULSE_UP_1 + (speed - 1) * 10;
  printf("[motor] request OPEN speed=%u from=%s pulse=%ums\r\n",
         (unsigned)speed, motor_state_str(s_state), pulse_ms);
  motor_send_command_pulses(pulse_ms);
  set_state(MOTOR_OPENING);
}

/**
  * @brief  执行下翻动作: 发下降指令
  */
static void motor_act_close(uint8_t speed)
{
  uint16_t pulse_ms = MOTOR_PULSE_DOWN_1 + (speed - 1) * 10;
  printf("[motor] request CLOSE speed=%u from=%s pulse=%ums\r\n",
         (unsigned)speed, motor_state_str(s_state), pulse_ms);
  motor_send_command_pulses(pulse_ms);
  set_state(MOTOR_CLOSING);
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

  /* 每次上电自动发送一次安装方式指令；左装配置会输出 90ms 脉冲。 */
  {
    motor_ret_t ret = motor_init();
    printf("[motor] boot install command: side=%s pulse=%ums result=%s\r\n",
           MOTOR_SIDE == MOTOR_SIDE_LEFT ? "LEFT" : "RIGHT",
           MOTOR_SIDE == MOTOR_SIDE_LEFT ? MOTOR_PULSE_INSTALL_LEFT : MOTOR_PULSE_INSTALL_RIGHT,
           ret == MOTOR_RET_OK ? "QUEUED" : "FAILED");
  }

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
          /* 丢弃关盖命令前积压的旧到位心跳，避免把旧状态误判为本次动作反馈。 */
          while (xQueueReceive(s_fb_q, &w_us, 0) == pdPASS) { }
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
      TickType_t fb_now = xTaskGetTickCount();
      TickType_t fb_gap = s_fb_rx_seen ? fb_now - s_last_fb_rx_tick : 0;
      fb_event_t event = fb_decode(w_us);
      TickType_t log_period = (s_state == MOTOR_INSTALLING ||
                               s_state == MOTOR_OPENING ||
                               s_state == MOTOR_CLOSING) ? pdMS_TO_TICKS(500) : pdMS_TO_TICKS(2000);
      static fb_event_t last_logged_event = FB_NONE;
      static TickType_t last_logged_tick = 0;
      static uint8_t have_logged_feedback = 0;

      /* 反馈可能持续广播；类别变化立即打印，同一类别限频，避免串口日志拖慢任务。 */
      if(!have_logged_feedback ||
         (event != last_logged_event &&
          (TickType_t)(fb_now - last_logged_tick) >= pdMS_TO_TICKS(100)) ||
         (TickType_t)(fb_now - last_logged_tick) >= log_period) {
        printf("[motor] feedback width=%luus code=%s state=%s gap=%lums\r\n",
               (unsigned long)w_us, fb_event_str(event), motor_state_str(s_state),
               (unsigned long)fb_gap);
        last_logged_event = event;
        last_logged_tick = fb_now;
        have_logged_feedback = 1;
      }
      s_last_fb_rx_tick = fb_now;
      s_fb_rx_seen = 1;
      fb_accept(event);
    }

    /* --- 3. 无异常检测: 位置只由反馈线上报的到位/过程码决定 --- */
    (void)now;
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

  if (s_cmd_q == NULL)
    return MOTOR_RET_NOT_INIT;
  if(s_command_pending) return MOTOR_RET_BUSY;   /* 上一条命令还没被任务取走 */

  /* 不再按状态拒绝: 开关盖只是把脉宽指令发给电机, 位置由反馈决定 */
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
