#include "app_service.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <string.h>

static QueueHandle_t s_commands;
static SemaphoreHandle_t s_lock;
static app_snapshot_t s_published;
static app_snapshot_t s_app; /* Only app_service_task mutates this instance. */
static uint32_t s_next_id, s_pending_id, s_write_started_ms;
static int32_t s_requested_target;
static uint8_t s_auto_write;

__WEAK int app_board_buzzer_set(int enabled) { (void)enabled; return 0; }

int app_service_init(void)
{
    memset(&s_app, 0, sizeof(s_app));
    incubator_model_init(&s_app.run);
    s_published = s_app;
    s_commands = xQueueCreate(8, sizeof(app_command_t));
    s_lock = xSemaphoreCreateMutex();
    return s_commands && s_lock;
}

int app_service_submit(app_command_kind_t kind, int32_t value)
{
    app_command_t c = {kind, value};
    return s_commands && xQueueSend(s_commands, &c, 0) == pdPASS;
}

void app_service_snapshot(app_snapshot_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_published;
    xSemaphoreGive(s_lock);
}

static int request_target(int32_t target, int automatic, uint32_t now)
{
    if(s_app.target_pending) return 0;
    if(++s_next_id == 0) ++s_next_id;
    if(!temp_service_set_target(target, s_next_id)) return 0;
    s_pending_id = s_next_id;
    s_requested_target = target;
    s_auto_write = automatic ? 1 : 0;
    s_app.target_pending = 1;
    s_write_started_ms = now;
    return 1;
}

static void command(const app_command_t *c, uint32_t now, uint8_t *door_request)
{
    s_app.message = APP_MSG_NONE;
    switch(c->kind) {
    case APP_SET_TEMP:
        /* Changing setpoint during a run is deliberately rejected: the
         * configured incubation conditions belong to that run's snapshot. */
        if(incubator_model_active(&s_app.run) && s_app.run.phase != INC_STORAGE)
            s_app.message = APP_MSG_SETTING_LOCKED;
        else if(c->value < APP_TEMP_MIN_CENTI || c->value > APP_TEMP_MAX_CENTI)
            s_app.message = APP_MSG_SET_TEMP;
        else if(!request_target(c->value, 0, now)) s_app.message = APP_MSG_BUSY;
        break;
    case APP_SET_TIME:
    case APP_SET_DELAY:
        if(incubator_model_active(&s_app.run)) s_app.message = APP_MSG_SETTING_LOCKED;
        else if(c->kind == APP_SET_TIME && c->value >= APP_TIME_MIN_TENTHS && c->value <= APP_TIME_MAX_TENTHS)
            s_app.run.time_tenths_min = (uint16_t)c->value;
        else if(c->kind == APP_SET_DELAY && c->value >= 0 && c->value <= APP_DELAY_MAX_S)
            s_app.run.delay_s = (uint16_t)c->value;
        break;
    case APP_START:
        if(s_app.target_pending || *door_request || motor_command_pending() || incubator_model_active(&s_app.run))
            s_app.message = APP_MSG_BUSY;
        else if(!s_app.temp.online || !s_app.temp.valid || s_app.temp.alarm)
            s_app.message = APP_MSG_TEMP_OFFLINE;
        else if(!s_app.temp.target_valid || s_app.run.phase == INC_ERROR)
            s_app.message = APP_MSG_SET_TEMP;
        else if(APP_REQUIRE_CLOSED && (motor_get_state() != MOTOR_CLOSED || s_app.door_fault))
            s_app.message = APP_MSG_CLOSE_LID;
        else incubator_model_start(&s_app.run, now);
        break;
    case APP_MUTE:
        incubator_model_mute(&s_app.run); /* No change to phase/deadlines. */
        break;
    case APP_OPEN:
    case APP_CLOSE:
        if(*door_request || s_app.door == MOTOR_INSTALLING ||
           s_app.door == MOTOR_OPENING || s_app.door == MOTOR_CLOSING) {
            s_app.message = APP_MSG_BUSY;
        } else if(s_app.door == MOTOR_UNINIT || s_app.door == MOTOR_FAULT || s_app.door_fault) {
            /* Explicit first door action authorizes the required calibration.
             * Never move the lid merely because MCU power was applied. */
            if(motor_init() == MOTOR_RET_OK) *door_request = c->kind == APP_OPEN ? 1 : 2;
            else s_app.message = APP_MSG_DOOR_FAILED;
        } else if(s_app.door_fault ||
                  (c->kind == APP_OPEN ? motor_open(MOTOR_SPEED_2) : motor_close(MOTOR_SPEED_2)) != MOTOR_RET_OK)
            s_app.message = APP_MSG_DOOR_FAILED;
        break;
    }
}

void app_service_task(void *argument)
{
    app_command_t c;
    uint8_t door_request = 0, previous_buzzer = 2;
    uint32_t door_request_ms = 0;
    uint8_t at_temp = 0;
    (void)argument;
    for(;;) {
        uint32_t now = HAL_GetTick();
        int32_t diff;
        temp_service_snapshot(&s_app.temp);
        s_app.door = motor_get_state();
        s_app.door_fault = s_app.door == MOTOR_FAULT ||
            ((s_app.door == MOTOR_OPEN || s_app.door == MOTOR_CLOSED) && motor_feedback_age_ms() > 2500u);
        if(s_app.target_pending && s_app.temp.completed_id == s_pending_id) {
            s_app.target_pending = 0;
            s_app.write_fault = !s_app.temp.write_ok;
            if(s_auto_write) incubator_model_cooling_result(&s_app.run, s_app.temp.write_ok);
            else if(s_app.temp.write_ok) {
                s_app.run.target_centi = s_requested_target;
                s_app.run.phase = INC_IDLE;
            }
            if(!s_app.temp.write_ok) s_app.message = APP_MSG_WRITE_FAILED;
        }
        if(s_app.target_pending && (uint32_t)(now-s_write_started_ms) > 10000u) {
            /* Keep command interlock until completion: never queue a second SV
             * while an unknown first transaction may still reach the module. */
            s_app.message = APP_MSG_WRITE_FAILED;
            s_app.write_fault = 1;
        }
        while(xQueueReceive(s_commands, &c, 0) == pdPASS) {
            uint8_t before = door_request;
            command(&c, now, &door_request);
            if(!before && door_request) door_request_ms = now;
        }
        if(door_request) {
            if(s_app.door == MOTOR_OPEN || s_app.door == MOTOR_CLOSED) {
                motor_ret_t ret = door_request == 1 ? motor_open(MOTOR_SPEED_2) : motor_close(MOTOR_SPEED_2);
                if(ret == MOTOR_RET_OK) door_request = 0;
            }
            if((uint32_t)(now-door_request_ms) > 18000u) {
                door_request = 0;
                s_app.message = APP_MSG_DOOR_FAILED;
            }
        }
        incubator_model_step(&s_app.run, now);
        if(s_app.run.cooling_request) {
            if(request_target(400, 1, now)) s_app.run.cooling_request = 0;
        }
        diff = s_app.temp.pv_centi - s_app.temp.applied_centi;
        if(diff < 0) diff = -diff;
        if(!s_app.temp.valid || !s_app.temp.target_valid || s_app.target_pending) at_temp = 0;
        else if(diff <= 30) at_temp = 1; /* Enter at +/-0.3C. */
        else if(diff > 50) at_temp = 0;  /* Exit at +/-0.5C; no status flicker. */
        s_app.at_temperature = at_temp;
        incubator_model_at_storage(&s_app.run, at_temp && s_app.temp.applied_centi == 400);
        s_app.alarm = !s_app.temp.online || s_app.temp.alarm || s_app.door_fault || s_app.write_fault;
        if(previous_buzzer != s_app.run.buzzer) {
            s_app.buzzer_hardware = app_board_buzzer_set(s_app.run.buzzer) ? 1 : 0;
            previous_buzzer = s_app.run.buzzer;
        }
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s_published = s_app;
        xSemaphoreGive(s_lock);
        osDelay(20);
    }
}
