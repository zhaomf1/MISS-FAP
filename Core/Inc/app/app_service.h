#ifndef APP_SERVICE_H
#define APP_SERVICE_H
#include "incubator_model.h"
#include "temp_service.h"
#include "motor.h"

typedef enum {
    APP_SET_TEMP, APP_SET_TIME, APP_SET_DELAY, APP_START,
    APP_MUTE, APP_OPEN, APP_CLOSE
} app_command_kind_t;
typedef struct { app_command_kind_t kind; int32_t value; } app_command_t;
typedef enum {
    APP_MSG_NONE, APP_MSG_BUSY, APP_MSG_SET_TEMP, APP_MSG_CLOSE_LID,
    APP_MSG_TEMP_OFFLINE, APP_MSG_WRITE_FAILED, APP_MSG_DOOR_FAILED,
    APP_MSG_SETTING_LOCKED
} app_message_t;
typedef struct {
    incubator_model_t run;
    temp_snapshot_t temp;
    motor_state_t door;
    uint8_t door_fault, target_pending, at_temperature, alarm, buzzer_hardware;
    uint8_t write_fault; /* Latched until a subsequent successful setpoint write. */
    app_message_t message;
} app_snapshot_t;

int app_service_init(void);
int app_service_submit(app_command_kind_t kind, int32_t value);
void app_service_snapshot(app_snapshot_t *out);
void app_service_task(void *argument);
/* Board hook: implement using the confirmed GPIO/PWM. Default returns 0:
 * request is visible in the UI, but no physical buzzer is claimed. */
int app_board_buzzer_set(int enabled);
#endif
