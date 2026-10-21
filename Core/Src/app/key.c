#include "key.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdio.h>

#define KEY_ACTIVE_LOW       1
#define KEY_SAMPLE_MS        10
#define KEY_DEBOUNCE_COUNT   2
#define KEY_DEBUG_LOG        1

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
    uint8_t raw;
    uint8_t stable;
    uint8_t count;
} key_pin_t;

static key_pin_t s_keys[KEY_COUNT] = {
    {GPIOB, GPIO_PIN_9, 0, 0, 0}, /* 左选择 */
    {GPIOE, GPIO_PIN_0, 0, 0, 0}, /* 右选择 */
    {GPIOE, GPIO_PIN_1, 0, 0, 0}, /* 确认 */
    {GPIOC, GPIO_PIN_6, 0, 0, 0}, /* 上选择 */
    {GPIOC, GPIO_PIN_7, 0, 0, 0}  /* 下选择 */
};

static QueueHandle_t s_key_queue;

#if KEY_DEBUG_LOG
static const char *key_name(key_id_t key)
{
    switch (key) {
    case KEY_LEFT:  return "LEFT(PB9)";
    case KEY_RIGHT: return "RIGHT(PE0)";
    case KEY_OK:    return "OK(PE1)";
    case KEY_UP:    return "UP(PC6)";
    case KEY_DOWN:  return "DOWN(PC7)";
    default:        return "UNKNOWN";
    }
}
#endif

static void key_gpio_init(void)
{
    GPIO_InitTypeDef init = {0};
    init.Mode = GPIO_MODE_INPUT;
    init.Pull = KEY_ACTIVE_LOW ? GPIO_PULLUP : GPIO_PULLDOWN;
    init.Speed = GPIO_SPEED_FREQ_LOW;

    init.Pin = GPIO_PIN_9;
    HAL_GPIO_Init(GPIOB, &init);
    init.Pin = GPIO_PIN_0 | GPIO_PIN_1;
    HAL_GPIO_Init(GPIOE, &init);
    init.Pin = GPIO_PIN_6 | GPIO_PIN_7;
    HAL_GPIO_Init(GPIOC, &init);
}

static uint8_t key_read_raw(const key_pin_t *key)
{
    GPIO_PinState level = HAL_GPIO_ReadPin(key->port, key->pin);
    return KEY_ACTIVE_LOW ? (level == GPIO_PIN_RESET) : (level == GPIO_PIN_SET);
}

static void key_push_event(key_id_t id, key_event_type_t type)
{
    key_event_t event = {id, type};
    if (s_key_queue != NULL) {
        (void)xQueueSend(s_key_queue, &event, 0);
    }
}

void key_task(void *argument)
{
    uint32_t i;
    (void)argument;

    s_key_queue = xQueueCreate(8, sizeof(key_event_t));
    configASSERT(s_key_queue != NULL);
    key_gpio_init();

#if KEY_DEBUG_LOG
    printf("[key] init: LEFT=PB9 RIGHT=PE0 OK=PE1 UP=PC6 DOWN=PC7, active-low, debounce=20ms\r\n");
#endif

    for (;;) {
        osDelay(KEY_SAMPLE_MS);
        for (i = 0; i < KEY_COUNT; ++i) {
            key_pin_t *key = &s_keys[i];
            uint8_t raw = key_read_raw(key);

            if (raw != key->raw) {
                key->raw = raw;
                key->count = 1;
            } else if (raw != key->stable && ++key->count >= KEY_DEBOUNCE_COUNT) {
                key->stable = raw;
                key->count = 0;
                key_push_event((key_id_t)i,
                               raw ? KEY_EVENT_PRESS : KEY_EVENT_RELEASE);
#if KEY_DEBUG_LOG
                printf("[key] %s %s\r\n", key_name((key_id_t)i),
                       raw ? "PRESS" : "RELEASE");
#endif
            }
        }
    }
}

int key_get_event(key_event_t *event)
{
    if (event == NULL || s_key_queue == NULL) return 0;
    return xQueueReceive(s_key_queue, event, 0) == pdPASS ? 1 : 0;
}

int key_is_pressed(key_id_t key)
{
    if ((unsigned)key >= KEY_COUNT) return 0;
    return s_keys[key].stable ? 1 : 0;
}
