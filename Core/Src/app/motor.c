/**
  ******************************************************************************
  * @file    motor.c
  * @brief   SND2506 翻盖电机驱动: TIM4_CH2 输入捕获测反馈 + PD14 GPIO 发脉宽指令
  *          + FreeRTOS 状态机服务任务 + USART1 调试命令口
  *
  * 注意: TIM4 被 PD13 输入捕获复用后, PD12(STEP_4)/PD15(LED_B) 的 PWM 输出
  *       不再可用 (当前工程未启用, 无影响)。若日后 CubeMX 重新生成代码,
  *       本模块会在运行时自行重配 TIM4/PD13/PD14, 无需依赖 .ioc 的改动。
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "motor.h"
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "cmsis_os.h"
#include "tim.h"      /* htim4 */
#include "usart.h"    /* huart1 */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

/* Private defines -----------------------------------------------------------*/

/* 指令输出脚 RGB-G*/
#define MOTOR_CMD_PORT           GPIOD
#define MOTOR_CMD_PIN            GPIO_PIN_14

/* 反馈输入脚 (TIM4_CH2, AF2) LED-R*/
#define MOTOR_FB_PORT            GPIOD
#define MOTOR_FB_PIN             GPIO_PIN_13

/* TIM4 计数时钟: 84MHz / (83+1) = 1MHz -> 1us 分辨率, 16bit 量程 65.5ms */
#define MOTOR_TIM4_PRESCALER     83

#if MOTOR_CMD_INVERTED
/* 方案A: 开漏经2N7002反相. 空闲=输出高(MOS导通, Input=0V), 脉冲=拉低 */
#define CMD_IDLE_LEVEL           GPIO_PIN_SET
#define CMD_ACTIVE_LEVEL         GPIO_PIN_RESET
#define CMD_GPIO_MODE            GPIO_MODE_OUTPUT_OD
#else
/* 方案C: 推挽3.3V直推. 空闲=输出低, 脉冲=输出高 */
#define CMD_IDLE_LEVEL           GPIO_PIN_RESET
#define CMD_ACTIVE_LEVEL         GPIO_PIN_SET
#define CMD_GPIO_MODE            GPIO_MODE_OUTPUT_PP
#endif

/* 反馈解码事件 */
typedef enum {
  FB_NONE = 0,
  FB_OPEN_DONE,
  FB_CLOSED_DONE,
  FB_UP_DECEL,
  FB_DOWN_DECEL,
  FB_MOVING,
  FB_UNKNOWN,
} fb_event_t;

/* 服务任务命令 */
typedef enum { MCMD_OPEN = 0, MCMD_CLOSE, MCMD_RAW, MCMD_REINIT } mcmd_type_t;

typedef struct {
  mcmd_type_t type;
  uint8_t     speed;   /* 1~3 档 (MCMD_RAW 时为脉宽低字节, 见 raw_ms) */
  uint16_t    raw_ms;  /* MCMD_RAW 用 */
} motor_cmd_t;

/* 反馈原始数据环形缓存 (调试 d 命令 dump) */
#define FB_RING_N   32

typedef struct {
  uint32_t w_us;       /* 低脉宽 us */
  uint32_t per_us;     /* 脉冲周期 us (上升沿到上升沿) */
  uint8_t  event;      /* 解码结果 fb_event_t */
} fb_ring_item_t;

/* Private variables ---------------------------------------------------------*/

static QueueHandle_t     s_cmd_q;      /* motor_cmd_t, 深度1: 忙时直接拒收 */
static QueueHandle_t     s_fb_q;       /* fb_ring_item_t, ISR->task */
static QueueHandle_t     s_cli_q;      /* uint8_t 字符, UART ISR->CLI task */

static volatile motor_state_t s_state = MOTOR_UNINIT;
static volatile uint8_t       s_dir_up = 1;      /* 当前指令方向: 1上翻 0下翻 */
static volatile TickType_t   s_fb_last_tick = 0; /* 最近一次反馈时刻 */
static volatile TickType_t   s_deadline = 0;     /* 当前阶段超时时刻 */
static volatile TickType_t   s_move_start = 0;   /* 本次动作发起时刻(宽限期用) */
static volatile uint8_t      s_fb_seen = 0;      /* 是否收到过任何反馈 */

static volatile uint32_t    s_cap_rise = 0;       /* ISR: 最近上升沿捕获值 */
static volatile uint32_t    s_cap_last_rise = 0;
static volatile uint32_t    s_per_us = 0;         /* ISR: 最近一次周期 */

static fb_ring_item_t  s_ring[FB_RING_N];
static volatile uint32_t s_ring_head = 0;

static uint8_t s_rx_byte;                        /* UART 单字节接收缓冲 */

/* Private function prototypes ------------------------------------------------*/
static void motor_hw_init(void);
static void motor_do_install(void);
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

  /* --- PD14: 指令输出. 先写空闲电平再切输出, 防止上电毛刺 --- */
  HAL_GPIO_WritePin(MOTOR_CMD_PORT, MOTOR_CMD_PIN, CMD_IDLE_LEVEL);
  gpio.Pin   = MOTOR_CMD_PIN;
  gpio.Mode  = CMD_GPIO_MODE;
  gpio.Pull  = GPIO_NOPULL;
  gpio.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(MOTOR_CMD_PORT, &gpio);

  /* --- PD13: TIM4_CH2 输入捕获 (AF2) --- */
  gpio.Pin       = MOTOR_FB_PIN;
  gpio.Mode      = GPIO_MODE_AF_PP;
  gpio.Pull      = MOTOR_FB_PULLUP ? GPIO_PULLUP : GPIO_NOPULL;
  gpio.Speed     = GPIO_SPEED_FREQ_LOW;
  gpio.Alternate = GPIO_AF2_TIM4;
  HAL_GPIO_Init(MOTOR_FB_PORT, &gpio);

  /* --- TIM4: 1MHz 计数 --- */
  htim4.Init.Prescaler = MOTOR_TIM4_PRESCALER;
  htim4.Init.Period    = 0xFFFFu;
  HAL_TIM_Base_Init(&htim4);

  /* CH1: 上升沿(低脉冲结束), IC1 经 INDIRECTTI 映射到 TI2 (即 PD13)
   * CH2: 下降沿(低脉冲开始), IC2 经 DIRECTTI  映射到 TI2            */
  ic.ICPrescaler = TIM_ICPSC_DIV1;
  ic.ICFilter    = 0x0F;                    /* 滤除 <3us 毛刺 */
  ic.ICPolarity  = TIM_ICPOLARITY_RISING;
  ic.ICSelection = TIM_ICSELECTION_INDIRECTTI;
  HAL_TIM_IC_ConfigChannel(&htim4, &ic, TIM_CHANNEL_1);
  ic.ICPolarity  = TIM_ICPOLARITY_FALLING;
  ic.ICSelection = TIM_ICSELECTION_DIRECTTI;
  HAL_TIM_IC_ConfigChannel(&htim4, &ic, TIM_CHANNEL_2);

  HAL_NVIC_SetPriority(TIM4_IRQn, 6, 0);    /* 低于 FreeRTOS 系统调用上限(5) */
  HAL_NVIC_EnableIRQ(TIM4_IRQn);
  HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_1);
  HAL_TIM_IC_Start_IT(&htim4, TIM_CHANNEL_2);
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
/* 中断服务 (TIM4 捕获 / USART1 接收)                                          */
/* ========================================================================== */

void TIM4_IRQHandler(void)
{
  HAL_TIM_IRQHandler(&htim4);
}

/**
  * @brief  TIM 输入捕获回调: CH1记上升沿(高脉冲开始), CH2记下降沿(结束)
  * @note   实测反馈极性与规格书相反: 空闲低电平, 每~34ms一个高脉冲,
  *         高脉冲宽度即状态代码 (0.8/2.5/3.3/5.0/7.5ms)
  */
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *htim)
{
  BaseType_t hprio_woken = pdFALSE;

  if (htim->Instance != TIM4)
    return;

  if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_1)          /* 上升沿: 高脉冲开始 */
  {
    uint32_t rise = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_1);
    /* 周期 = 上升沿到上升沿 */
    s_per_us = (rise >= s_cap_last_rise)
               ? (rise - s_cap_last_rise)
               : (rise + 0x10000u - s_cap_last_rise);
    s_cap_last_rise = rise;
    s_cap_rise      = rise;
  }
  else if (htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2)    /* 下降沿: 高脉冲结束 */
  {
    uint32_t fall = HAL_TIM_ReadCapturedValue(htim, TIM_CHANNEL_2);
    uint32_t rise = s_cap_rise;
    fb_ring_item_t item;

    /* 高脉冲宽度 (16bit 回绕处理) */
    item.w_us   = (fall >= rise) ? (fall - rise) : (fall + 0x10000u - rise);
    item.per_us = s_per_us;
    item.event  = FB_NONE;
    xQueueSendFromISR(s_fb_q, &item, &hprio_woken);
  }
  portYIELD_FROM_ISR(hprio_woken);
}

/**
  * @brief  USART1 单字节接收完成 (HAL weak 回调重载)
  */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  BaseType_t hprio_woken = pdFALSE;

  if (huart->Instance == USART1)
  {
    xQueueSendFromISR(s_cli_q, &s_rx_byte, &hprio_woken);
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);   /* 重新武装 */
  }
  portYIELD_FROM_ISR(hprio_woken);
}

/**
  * @brief  UART 出错(过载/噪声等)后重新武装接收
  */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  if (huart->Instance == USART1)
  {
    HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);
  }
}

/* ========================================================================== */
/* 反馈解码 + 状态机                                                            */
/* ========================================================================== */

static fb_event_t fb_decode(uint32_t w_us)
{
  int32_t d;

  d = (int32_t)w_us - MOTOR_FB_OPEN_DONE_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_OPEN_DONE;
  d = (int32_t)w_us - MOTOR_FB_MOVING_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_MOVING;
  d = (int32_t)w_us - MOTOR_FB_UP_DECEL_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_UP_DECEL;
  d = (int32_t)w_us - MOTOR_FB_DOWN_DECEL_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_DOWN_DECEL;
  d = (int32_t)w_us - MOTOR_FB_CLOSED_DONE_US;
  if (d >= -MOTOR_FB_WIN_US && d <= MOTOR_FB_WIN_US)  return FB_CLOSED_DONE;
  return FB_UNKNOWN;
}

static const char *fb_event_str(uint8_t e)
{
  switch ((fb_event_t)e)
  {
    case FB_OPEN_DONE:   return "OPEN_DONE(0.8ms)";
    case FB_CLOSED_DONE: return "CLOSED_DONE(7.5ms)";
    case FB_UP_DECEL:    return "UP_DECEL(3.3ms)";
    case FB_DOWN_DECEL:  return "DOWN_DECEL(5.0ms)";
    case FB_MOVING:      return s_dir_up ? "UP_MOVING(2.5ms)" : "DOWN_MOVING(2.5ms)";
    case FB_UNKNOWN:     return "UNKNOWN";
    default:             return "-";
  }
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
  * @brief  状态机事件处理 (含2次连续相同解码消抖, 由调用方处理)
  */
static void fb_accept(fb_event_t e)
{
  s_fb_last_tick = xTaskGetTickCount();
  s_fb_seen = 1;

  switch (s_state)
  {
    case MOTOR_INSTALLING:
      /* 校准结束后停在哪个到位位置就同步哪个 (开/关都可能) */
      if (e == FB_CLOSED_DONE)      set_state(MOTOR_CLOSED);
      else if (e == FB_OPEN_DONE)   set_state(MOTOR_OPEN);
      break;

    case MOTOR_OPENING:
      if (e == FB_OPEN_DONE) set_state(MOTOR_OPEN);
      break;

    case MOTOR_CLOSING:
      if (e == FB_CLOSED_DONE) set_state(MOTOR_CLOSED);
      break;

    case MOTOR_OPEN:
      /* 堵转保护会自动缓降复位: 开着盖突然收到"关到位"就同步状态 */
      if (e == FB_CLOSED_DONE) set_state(MOTOR_CLOSED);
      break;

    case MOTOR_CLOSED:
      /* 手动翻开盖板等情况: 同步为开 */
      if (e == FB_OPEN_DONE) set_state(MOTOR_OPEN);
      break;

    default:
      break;
  }
}

/**
  * @brief  安装方式初始化: 每次电机上电后必须执行一次
  */
static void motor_do_install(void)
{
  uint16_t pulse_ms = (MOTOR_SIDE == MOTOR_SIDE_LEFT)
                      ? MOTOR_PULSE_INSTALL_LEFT : MOTOR_PULSE_INSTALL_RIGHT;

  printf("[motor] install %s, sending %ums pulse...\r\n",
         (MOTOR_SIDE == MOTOR_SIDE_LEFT) ? "LEFT" : "RIGHT", pulse_ms);
  motor_pulse(pulse_ms);
  set_state(MOTOR_INSTALLING);
  s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(MOTOR_INSTALL_TIMEOUT_MS);
}

/* ========================================================================== */
/* 服务任务                                                                     */
/* ========================================================================== */

void MotorTask(void *argument)
{
  (void)argument;
  motor_cmd_t cmd;
  fb_ring_item_t item;
  fb_event_t pend = FB_NONE;      /* 消抖: 连续2次相同才接受 */
  uint8_t    pend_cnt = 0;

  s_cmd_q = xQueueCreate(1, sizeof(motor_cmd_t));
  s_fb_q  = xQueueCreate(16, sizeof(fb_ring_item_t));
  s_cli_q = xQueueCreate(32, sizeof(uint8_t));
  configASSERT(s_cmd_q && s_fb_q && s_cli_q);

  motor_hw_init();
  printf("[motor] hw init done, waiting motor boot %ums...\r\n",
         MOTOR_STARTUP_DELAY_MS);
  osDelay(MOTOR_STARTUP_DELAY_MS);

#if MOTOR_AUTO_INSTALL
  motor_do_install();
#else
  /* 手动模式: 上电不动. 电机每次上电需先收到一次安装指令(40/90ms)
   * 才能正常执行升降, 由串口手动发: i40 / i90 (会自校准转动一次) */
  printf("[motor] AUTO_INSTALL off. type 'i40' or 'i90' to install first,\r\n"
         "       then 'i10'~'i30' forward / 'i50'~'i70' reverse (or o/c)\r\n");
#endif

  for (;;)
  {
    TickType_t now;

    /* --- 1. 处理命令 (深度1队列, 最多等50ms) --- */
    if (xQueueReceive(s_cmd_q, &cmd, pdMS_TO_TICKS(50)) == pdPASS)
    {
      switch (cmd.type)
      {
        case MCMD_OPEN:
          s_dir_up = 1;
          motor_pulse(MOTOR_PULSE_UP_1 + (cmd.speed - 1) * 10);  /* 10/20/30 */
          set_state(MOTOR_OPENING);
          s_fb_last_tick = xTaskGetTickCount();
          s_move_start   = s_fb_last_tick;
          s_deadline = s_fb_last_tick + pdMS_TO_TICKS(MOTOR_MOVE_TIMEOUT_MS);
          break;

        case MCMD_CLOSE:
          s_dir_up = 0;
          motor_pulse(MOTOR_PULSE_DOWN_1 + (cmd.speed - 1) * 10); /* 50/60/70 */
          set_state(MOTOR_CLOSING);
          s_fb_last_tick = xTaskGetTickCount();
          s_move_start   = s_fb_last_tick;
          s_deadline = s_fb_last_tick + pdMS_TO_TICKS(MOTOR_MOVE_TIMEOUT_MS);
          break;

        case MCMD_RAW:
          motor_pulse(cmd.raw_ms);
          /* 手动模式: i40/i90 即安装方式设置, 状态机同步进入安装等待 */
          if (s_state == MOTOR_UNINIT &&
              (cmd.raw_ms == MOTOR_PULSE_INSTALL_LEFT ||
               cmd.raw_ms == MOTOR_PULSE_INSTALL_RIGHT))
          {
            set_state(MOTOR_INSTALLING);
            s_deadline = xTaskGetTickCount() + pdMS_TO_TICKS(MOTOR_INSTALL_TIMEOUT_MS);
          }
          break;

        case MCMD_REINIT:
          motor_do_install();
          break;
      }
    }

    /* --- 2. 处理反馈 (非阻塞排空) --- */
    while (xQueueReceive(s_fb_q, &item, 0) == pdPASS)
    {
      fb_event_t e = fb_decode(item.w_us);
      item.event = (uint8_t)e;

      s_ring[s_ring_head] = item;                 /* 调试缓存 */
      s_ring_head = (s_ring_head + 1) % FB_RING_N;

      s_fb_last_tick = xTaskGetTickCount();        /* 线路存活标记: 每条都算 */
      s_fb_seen = 1;

      if (e == pend)                               /* 连续2次相同才跳状态 */
      {
        if (++pend_cnt >= 2)
        {
          fb_accept(e);
          pend_cnt = 0;
        }
      }
      else
      {
        pend     = e;
        pend_cnt = 1;
      }
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
        /* 前1s为启动宽限期: 电机收到指令到开始发反馈可能有延迟 */
        if ((now - s_move_start) > pdMS_TO_TICKS(1000) &&
            (!s_fb_seen ||
             (now - s_fb_last_tick) > pdMS_TO_TICKS(MOTOR_FB_MOTION_LOST_MS)))
        {
          printf("[motor] FAULT: no fb while moving\r\n");
          set_state(MOTOR_FAULT);
        }
        else if ((int32_t)(now - s_deadline) >= 0)
        {
          printf("[motor] FAULT: move timeout\r\n");
          set_state(MOTOR_FAULT);
        }
        break;

      default:
        break;   /* 静止态不设超时: 电机到位后是否持续发脉冲待实测 */
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

static motor_ret_t send_cmd(mcmd_type_t type, uint8_t speed, uint16_t raw_ms)
{
  motor_state_t st = s_state;
  motor_cmd_t cmd = { type, speed, raw_ms };

  if (s_cmd_q == NULL)
    return MOTOR_RET_NOT_INIT;

  if (type != MCMD_RAW && type != MCMD_REINIT)
  {
    if (st == MOTOR_UNINIT || st == MOTOR_INSTALLING) return MOTOR_RET_NOT_INIT;
    if (st == MOTOR_OPENING || st == MOTOR_CLOSING)    return MOTOR_RET_BUSY;
    if (st == MOTOR_FAULT)                             return MOTOR_RET_FAULT;
  }

  if (xQueueSend(s_cmd_q, &cmd, 0) != pdPASS)
    return MOTOR_RET_BUSY;
  return MOTOR_RET_OK;
}

motor_ret_t motor_open(motor_speed_t speed)
{
  if (speed < MOTOR_SPEED_1) speed = MOTOR_SPEED_2;
  if (speed > MOTOR_SPEED_3) speed = MOTOR_SPEED_2;
  return send_cmd(MCMD_OPEN, (uint8_t)speed, 0);
}

motor_ret_t motor_close(motor_speed_t speed)
{
  if (speed < MOTOR_SPEED_1) speed = MOTOR_SPEED_2;
  if (speed > MOTOR_SPEED_3) speed = MOTOR_SPEED_2;
  return send_cmd(MCMD_CLOSE, (uint8_t)speed, 0);
}

motor_ret_t motor_reinit(void)
{
  return send_cmd(MCMD_REINIT, 0, 0);
}

motor_ret_t motor_send_raw_pulse(uint16_t width_ms)
{
  if (width_ms == 0)
    return MOTOR_RET_ERR;
  return send_cmd(MCMD_RAW, 0, width_ms);
}

/* ========================================================================== */
/* USART1 调试命令口 (业务触发的串口指令先落在这里)                              */
/*                                                                            */
/*  o[1-3]  开盖(默认2档)      c[1-3]  关盖(默认2档)                          */
/*  i<ms>   发原始脉冲         s       查询状态                                */
/*  d       dump 反馈原始数据   r       重发安装指令(FAULT恢复)                */
/*  h       帮助                                                               */
/* ========================================================================== */

static void cli_help(void)
{
  printf("\r\n--- motor cli ---\r\n"
         " o[1-3] open  (speed, default 2)\r\n"
         " c[1-3] close (speed, default 2)\r\n"
         " i<ms>  raw pulse: i40/i90=install, i10/20/30=fwd, i50/60/70=rev\r\n"
         " s      status\r\n"
         " d      dump feedback log\r\n"
         " r      resend install pulse\r\n"
         " h      help\r\n");
}

static void cli_status(void)
{
  printf("[motor] state=%s fb=%s fb_age=%ums\r\n", motor_state_str(s_state),
         s_fb_seen ? "seen" : "never",
         s_fb_seen ? (uint32_t)(xTaskGetTickCount() - s_fb_last_tick) : 0u);
}

static void cli_dump(void)
{
  printf("[motor] feedback log (oldest first):\r\n");
  for (uint32_t i = 0; i < FB_RING_N; i++)
  {
    uint32_t idx = (s_ring_head + i) % FB_RING_N;
    if (s_ring[idx].w_us == 0 && s_ring[idx].per_us == 0)
      continue;
    printf("  hi_w=%6lu us  per=%7lu us  %s\r\n",
           (unsigned long)s_ring[idx].w_us,
           (unsigned long)s_ring[idx].per_us,
           fb_event_str(s_ring[idx].event));
  }
}

static void cli_exec(char *s)
{
  uint8_t speed = 2;
  motor_ret_t ret;

  switch (tolower((unsigned char)s[0]))
  {
    case 'o':
    case 'c':
      if (isdigit((unsigned char)s[1]))
        speed = (uint8_t)(s[1] - '0');
      if (tolower(s[0]) == 'o')
        ret = motor_open((motor_speed_t)speed);
      else
        ret = motor_close((motor_speed_t)speed);
      printf("[cli] %s -> %d\r\n", (tolower(s[0]) == 'o') ? "open" : "close", (int)ret);
      break;

    case 'i':
      if (s[1] != '\0')
      {
        ret = motor_send_raw_pulse((uint16_t)atoi(&s[1]));
        printf("[cli] raw -> %d\r\n", ret);
      }
      break;

    case 's': cli_status(); break;
    case 'd': cli_dump();   break;
    case 'r':
      ret = motor_reinit();
      printf("[cli] reinit -> %d\r\n", ret);
      break;
    case 'h': cli_help();   break;
    default:  break;
  }
}

void CliTask(void *argument)
{
  (void)argument;
  uint8_t ch;
  char buf[12];
  int n = 0;

  HAL_UART_Receive_IT(&huart1, &s_rx_byte, 1);

  printf("\r\n[motor] cli ready (115200-8N1), 'h' for help\r\n");
  cli_help();

  for (;;)
  {
    if (xQueueReceive(s_cli_q, &ch, portMAX_DELAY) == pdPASS)
    {
      if (ch == '\r' || ch == '\n')
      {
        if (n > 0)
        {
          buf[n] = '\0';
          cli_exec(buf);
          n = 0;
        }
      }
      else if (n < (int)sizeof(buf) - 1)
      {
        buf[n++] = (char)ch;
        printf("%c", ch);            /* 本地回显 */
      }
    }
  }
}
