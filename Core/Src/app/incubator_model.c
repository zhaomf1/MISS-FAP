#include "incubator_model.h"
#include <string.h>

/*
 * 孵育状态机模块：只处理时间和阶段逻辑，不直接依赖 RTOS 或硬件。
 * 所有时间由应用服务传入，便于在主机测试环境中验证。
 */

/* 清零状态并装载默认温度、孵育时间和延迟降温参数。 */
void incubator_model_init(incubator_model_t *m)
{
    memset(m, 0, sizeof(*m));
    m->target_centi = 3700;
    m->time_s = 15u * 60u;
    m->delay_s = 60;
    m->phase = INC_IDLE;
}

/* 判断当前是否仍处于运行或运行结束后的处理阶段。 */
int incubator_model_active(const incubator_model_t *m)
{
    return m->phase == INC_RUNNING || m->phase == INC_DELAY ||
           m->phase == INC_COOL_REQUEST || m->phase == INC_COOLING ||
           m->phase == INC_STORAGE;
}

static uint32_t elapsed_ms(const incubator_model_t *m, uint32_t now_ms)
{
    uint32_t elapsed = now_ms - m->run_start_ms - m->paused_total_ms;
    if(m->paused) elapsed -= now_ms - m->pause_start_ms;
    return elapsed;
}

/* 仅允许从空闲阶段启动一次新的孵育计时。 */
int incubator_model_start(incubator_model_t *m, uint32_t now_ms)
{
    if(incubator_model_active(m) || m->time_s < APP_TIME_MIN_S ||
       m->time_s > APP_TIME_MAX_S) return 0;
    /* 一次运行使用启动时的参数快照，后续修改不能改变当前截止时间。 */
    m->run_start_ms = now_ms;
    m->run_duration_ms = m->time_s * 1000u;
    m->run_delay_ms = (uint32_t)m->delay_s * 1000u;
    m->remaining_ms = m->run_duration_ms;
    m->delay_remaining_ms = m->run_delay_ms;
    m->paused_total_ms = 0;
    m->pause_start_ms = 0;
    m->paused = 0;
    m->buzzer = m->cooling_request = 0;
    m->phase = INC_RUNNING;
    return 1;
}

/* 根据当前时间推进孵育、延迟、降温和保存阶段。 */
void incubator_model_step(incubator_model_t *m, uint32_t now_ms)
{
    uint32_t elapsed;
    if(m->paused) return;            /* 已暂停：不能推进任何截止时间。 */
    elapsed = elapsed_ms(m, now_ms);
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
            m->cooling_request = 1; /* 由应用服务消费一次。 */
        }
    }
}

/* 消除蜂鸣提示，不改变当前阶段和计时截止时间。 */
void incubator_model_mute(incubator_model_t *m) { m->buzzer = 0; }

/* 在温度或舱门异常时暂停计时，恢复后继续而不扣除暂停时间。 */
void incubator_model_set_paused(incubator_model_t *m, int paused, uint32_t now_ms)
{
    if(m->phase != INC_RUNNING && m->phase != INC_DELAY) return;
    if(paused && !m->paused) {
        m->paused = 1;
        m->pause_start_ms = now_ms;
    } else if(!paused && m->paused) {
        m->paused_total_ms += now_ms - m->pause_start_ms;
        m->paused = 0;
    }
}

/* 取消本次运行并返回可编辑的空闲阶段。 */
void incubator_model_stop(incubator_model_t *m)
{
    m->phase = INC_IDLE;
    m->buzzer = 0;
    m->cooling_request = 0;
    m->remaining_ms = 0;
    m->delay_remaining_ms = 0;
    m->paused = 0;
    m->paused_total_ms = 0;
    m->pause_start_ms = 0;
}

/* 处理自动降温设定写入结果，失败时进入错误阶段。 */
void incubator_model_cooling_result(incubator_model_t *m, int success)
{
    if(m->phase != INC_COOL_REQUEST) return;
    m->phase = success ? INC_COOLING : INC_ERROR;
}

/* 确认已经达到 4℃ 保存条件后进入保存阶段。 */
void incubator_model_at_storage(incubator_model_t *m, int at_temperature)
{
    if(m->phase == INC_COOLING && at_temperature) m->phase = INC_STORAGE;
    else if(m->phase == INC_STORAGE && !at_temperature) m->phase = INC_COOLING;
}
