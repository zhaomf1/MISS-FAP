/**
  ******************************************************************************
  * @file    key.c
  * @brief   5键扫描: 10ms 采样, 20ms 消抖, +/-键长按连发; 事件入队 + 可选回显
  ******************************************************************************
  */

#include "key.h"
#include <stdio.h>
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "usart.h"    /* huart6: PC6/PC7 运行时从 USART6 释放回 GPIO */

/* Private variables ---------------------------------------------------------*/

typedef struct {
  GPIO_TypeDef *port;
  uint16_t      pin;
  key_id_t      id;
  uint8_t       last_raw;      /* 上次原始采样 */
  uint8_t       filtered;      /* 消抖后状态: 1=按下 */
  uint8_t       cnt;           /* 原始采样连续一致计数 */
  TickType_t    press_tick;    /* 按下时刻 */
  TickType_t    last_repeat;    /* 上次连发时刻 */
} key_def_t;

static key_def_t s_keys[] = {
  { GPIOB, GPIO_PIN_9,  KEY_DEC,        0, 0, 0, 0, 0 },   //左键
  { GPIOE, GPIO_PIN_0,  KEY_INC,        0, 0, 0, 0, 0 },   //右键
  { GPIOE, GPIO_PIN_1,  KEY_SET,        0, 0, 0, 0, 0 },   //确认按键
  { GPIOC, GPIO_PIN_6,  KEY_DOOR_OPEN,  0, 0, 0, 0, 0 },   //开盖按键 (原 USART6_TX, 2026-10-16 回退)
  { GPIOC, GPIO_PIN_7,  KEY_DOOR_CLOSE, 0, 0, 0, 0, 0 },   //关盖按键 (原 USART6_RX, 2026-10-16 回退)
};

#define KEY_N  (sizeof(s_keys) / sizeof(s_keys[0]))

static QueueHandle_t s_evt_q;       /* key_event_t, 供 UI 任务后续消费 */
static volatile int  s_echo = 0;

/* Private functions ---------------------------------------------------------*/

static void key_pin_init(void)
{
  GPIO_InitTypeDef g = {0};

  /* PC6/PC7 在 CubeMX 里是 USART6 (本测试板不用), 运行时释放回 GPIO。
   * 必须先 DeInit 再 GPIO_Init: MspDeInit 里的 HAL_GPIO_DeInit 会把
   * PC6/PC7 打回模拟浮空态, 放在后面会覆盖刚配好的输入上拉 (开发记录22节教训)。
   * main.c 已注释 MX_USART6_UART_Init(), Instance 为 NULL 时跳过 */
  if (huart6.Instance != NULL)
    HAL_UART_DeInit(&huart6);

  g.Mode = GPIO_MODE_INPUT;
  g.Pull = KEY_ACTIVE_LOW ? GPIO_PULLUP : GPIO_PULLDOWN;

  g.Pin = GPIO_PIN_9;                          /* K1 PB9 */
  HAL_GPIO_Init(GPIOB, &g);
  g.Pin = GPIO_PIN_0 | GPIO_PIN_1;             /* K2 PE0, K3 PE1 */
  HAL_GPIO_Init(GPIOE, &g);
  g.Pin = GPIO_PIN_6 | GPIO_PIN_7;             /* 开/关舱门键 PC6 PC7 */
  HAL_GPIO_Init(GPIOC, &g);
}

const char *key_name(key_id_t id)
{
  switch (id)
  {
    case KEY_DEC:        return "left";
    case KEY_INC:        return "right";
    case KEY_SET:        return "set";
    case KEY_DOOR_OPEN:  return "open";
    case KEY_DOOR_CLOSE: return "close";
    default:             return "?";
  }
}

static const char *key_evt_name(key_evt_type_t type)
{
  switch (type)
  {
    case KEY_EVT_PRESS:   return "press";
    case KEY_EVT_REPEAT:  return "repeat";
    case KEY_EVT_RELEASE: return "raise";
    default:              return "?";
  }
}

/**
  * @brief 调试: 打印5个按键脚的实时电平 (CLI 'p') —— 判断有效电平/接线
  *        io=引脚原始电平(1=高), filtered=按当前有效电平判定的按下状态
  */
void key_dump_pins(void)
{
  uint32_t i;

  printf("[key] active=%s, pin levels:\r\n",
         KEY_ACTIVE_LOW ? "low" : "high");
  for (i = 0; i < KEY_N; i++)
  {
    key_def_t *k = &s_keys[i];
    GPIO_PinState st = HAL_GPIO_ReadPin(k->port, k->pin);
    char port = 'A';
    int bit;

    if (k->port == GPIOB)      port = 'B';
    else if (k->port == GPIOC)  port = 'C';
    else if (k->port == GPIOD)  port = 'D';
    else if (k->port == GPIOE)  port = 'E';
    for (bit = 0; bit < 16; bit++)
      if (k->pin & (1u << bit))
        break;

    printf("  %-5s P%c%-2d io=%u filtered=%u\r\n",
           key_name(k->id), port, bit,
           (unsigned)(st == GPIO_PIN_SET), (unsigned)k->filtered);
  }
}

static void key_emit(key_def_t *k, key_evt_type_t type)
{
  key_event_t e = { k->id, type };

  if (s_evt_q)
    xQueueSend(s_evt_q, &e, 0);
  if (s_echo)
    printf("[key] %s-key %s\r\n", key_name(k->id), key_evt_name(type));
}

/* ========================================================================== */
/* 任务                                                                        */
/* ========================================================================== */

void KeyTask(void *argument)
{
  (void)argument;
  uint32_t i;

  s_evt_q = xQueueCreate(8, sizeof(key_event_t));
  configASSERT(s_evt_q);
  key_pin_init();
  printf("[key] init done, %u keys\r\n", (unsigned)KEY_N);

  for (;;)
  {
    osDelay(10);                              /* 10ms 采样周期 */

    for (i = 0; i < KEY_N; i++)
    {
      key_def_t *k = &s_keys[i];
      GPIO_PinState st = HAL_GPIO_ReadPin(k->port, k->pin);
      uint8_t raw = KEY_ACTIVE_LOW ? (st == GPIO_PIN_RESET)
                                   : (st == GPIO_PIN_SET);
      TickType_t now = xTaskGetTickCount();

      /* --- 消抖 --- */
      if (raw != k->last_raw)
      {
        k->last_raw = raw;
        k->cnt = 1;
      }
      else if (raw != k->filtered && ++k->cnt >= KEY_DEBOUNCE_N)
      {
        k->filtered = raw;
        if (raw)                               /* 按下沿 */
        {
          k->press_tick  = now;
          k->last_repeat = now;
          key_emit(k, KEY_EVT_PRESS);
        }
        else                                   /* 抬起沿 */
        {
          key_emit(k, KEY_EVT_RELEASE);
        }
      }

      /* --- +/-键长按连发 --- */
#if KEY_AUTO_REPEAT
      if (k->filtered && (k->id == KEY_DEC || k->id == KEY_INC))
      {
        if ((now - k->press_tick)  >= pdMS_TO_TICKS(KEY_REPEAT_DELAY_MS) &&
            (now - k->last_repeat) >= pdMS_TO_TICKS(KEY_REPEAT_RATE_MS))
        {
          k->last_repeat = now;
          key_emit(k, KEY_EVT_REPEAT);
        }
      }
#endif
    }
  }
}

/* ========================================================================== */
/* 调试接口                                                                     */
/* ========================================================================== */

void key_set_echo(int on)
{
  s_echo = on;
}

int key_toggle_echo(void)
{
  s_echo = !s_echo;
  return s_echo;
}
