#ifndef INCUBATOR_MODEL_H
#define INCUBATOR_MODEL_H
#include <stdint.h>

/* Provisional operating bounds; keep product policy separate from UI/driver. */
#define APP_TEMP_MIN_CENTI 400
#define APP_TEMP_MAX_CENTI 6000
#define APP_TIME_MIN_TENTHS 1
#define APP_TIME_MAX_TENTHS 999
#define APP_DELAY_MAX_S 300
#define APP_REQUIRE_CLOSED 1

typedef enum {
    INC_IDLE, INC_RUNNING, INC_FINISHED, INC_DELAY,
    INC_COOL_REQUEST, INC_COOLING, INC_STORAGE, INC_ERROR
} inc_phase_t;

typedef struct {
    int32_t target_centi;           /* User liquid target, not module raw SV. */
    uint16_t time_tenths_min;
    uint16_t delay_s;               /* 0 = N, automatic cooling disabled. */
    inc_phase_t phase;
    uint32_t run_start_ms, run_duration_ms, run_delay_ms;
    uint32_t remaining_ms, delay_remaining_ms;
    uint8_t buzzer, cooling_request;
} incubator_model_t;

void incubator_model_init(incubator_model_t *m);
int incubator_model_start(incubator_model_t *m, uint32_t now_ms);
void incubator_model_step(incubator_model_t *m, uint32_t now_ms);
void incubator_model_mute(incubator_model_t *m);
void incubator_model_cooling_result(incubator_model_t *m, int success);
void incubator_model_at_storage(incubator_model_t *m, int at_temperature);
int incubator_model_active(const incubator_model_t *m);
#endif
