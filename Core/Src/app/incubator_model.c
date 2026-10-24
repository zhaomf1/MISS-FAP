#include "incubator_model.h"
#include <string.h>

void incubator_model_init(incubator_model_t *m)
{
    memset(m, 0, sizeof(*m));
    m->target_centi = 3700;
    m->time_tenths_min = 150;
    m->delay_s = 60;
    m->phase = INC_IDLE;
}

int incubator_model_active(const incubator_model_t *m)
{
    return m->phase == INC_RUNNING || m->phase == INC_DELAY ||
           m->phase == INC_COOL_REQUEST || m->phase == INC_COOLING ||
           m->phase == INC_STORAGE;
}

int incubator_model_start(incubator_model_t *m, uint32_t now_ms)
{
    if(incubator_model_active(m) || m->time_tenths_min < APP_TIME_MIN_TENTHS ||
       m->time_tenths_min > APP_TIME_MAX_TENTHS) return 0;
    /* A run owns a snapshot: later setting changes cannot alter its deadlines. */
    m->run_start_ms = now_ms;
    m->run_duration_ms = (uint32_t)m->time_tenths_min * 6000u;
    m->run_delay_ms = (uint32_t)m->delay_s * 1000u;
    m->remaining_ms = m->run_duration_ms;
    m->delay_remaining_ms = m->run_delay_ms;
    m->buzzer = m->cooling_request = 0;
    m->phase = INC_RUNNING;
    return 1;
}

void incubator_model_step(incubator_model_t *m, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - m->run_start_ms; /* Wrap-safe for bounded runs. */
    if(m->phase == INC_RUNNING) {
        m->remaining_ms = elapsed < m->run_duration_ms ? m->run_duration_ms - elapsed : 0;
        if(m->remaining_ms == 0) {
            m->buzzer = 1;
            m->phase = m->run_delay_ms ? INC_DELAY : INC_FINISHED;
        }
    }
    if(m->phase == INC_DELAY) {
        uint32_t after_finish = elapsed - m->run_duration_ms;
        m->delay_remaining_ms = after_finish < m->run_delay_ms ? m->run_delay_ms - after_finish : 0;
        if(m->delay_remaining_ms == 0) {
            m->phase = INC_COOL_REQUEST;
            m->cooling_request = 1; /* Consumed once by the application service. */
        }
    }
}

void incubator_model_mute(incubator_model_t *m) { m->buzzer = 0; }

void incubator_model_cooling_result(incubator_model_t *m, int success)
{
    if(m->phase != INC_COOL_REQUEST) return;
    m->phase = success ? INC_COOLING : INC_ERROR;
}

void incubator_model_at_storage(incubator_model_t *m, int at_temperature)
{
    if(m->phase == INC_COOLING && at_temperature) m->phase = INC_STORAGE;
    else if(m->phase == INC_STORAGE && !at_temperature) m->phase = INC_COOLING;
}
