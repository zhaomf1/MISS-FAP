#ifndef INCUBATOR_MODEL_H
#define INCUBATOR_MODEL_H
#include <stdint.h>

/* Product policy. Temperature 0.0-60.0 C, incubation 0.1-99.9 min, delay 0-300 s. */
#define APP_TEMP_MIN_CENTI 0
#define APP_TEMP_MAX_CENTI 6000

#define APP_TIME_MIN_S     6u
#define APP_TIME_MAX_S     5994u
#define APP_DELAY_MAX_S    300u

#define APP_REQUIRE_CLOSED 1

typedef enum {
    INC_IDLE, INC_RUNNING, INC_FINISHED, INC_DELAY,
    INC_COOL_REQUEST, INC_COOLING, INC_STORAGE, INC_ERROR
} inc_phase_t;

typedef struct {
    int32_t target_centi;           /* User liquid target, not module raw SV. */
    uint32_t time_s;                /* Configured incubation time, seconds.   */
    uint16_t delay_s;               /* 0 = N, automatic cooling disabled.    */
    inc_phase_t phase;
    uint32_t run_start_ms, run_duration_ms, run_delay_ms;
    uint32_t remaining_ms, delay_remaining_ms;
    uint32_t paused_total_ms, pause_start_ms; /* Standby excluded from the run. */
    uint8_t paused;
    uint8_t buzzer, cooling_request;
} incubator_model_t;

void incubator_model_init(incubator_model_t *m);
int incubator_model_start(incubator_model_t *m, uint32_t now_ms);
void incubator_model_step(incubator_model_t *m, uint32_t now_ms);
void incubator_model_mute(incubator_model_t *m);
/* Freeze/continue the countdown; only RUNNING and DELAY are affected.
 * Time spent frozen is not deducted from the configured incubation time. */
void incubator_model_set_paused(incubator_model_t *m, int paused, uint32_t now_ms);
/* Cancel the run and return to the idle/setup phase. */
void incubator_model_stop(incubator_model_t *m);
void incubator_model_cooling_result(incubator_model_t *m, int success);
void incubator_model_at_storage(incubator_model_t *m, int at_temperature);
int incubator_model_active(const incubator_model_t *m);
#endif
