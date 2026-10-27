#ifndef INCUBATOR_MODEL_H
#define INCUBATOR_MODEL_H
#include <stdint.h>

/** @file incubator_model.h
 *  @brief 与硬件无关的孵育状态机和倒计时策略。
 */

/* 产品范围：温度 0.0~60.0℃，孵育时间 0.1~99.9 分钟，延迟 0~300 秒。 */
#define APP_TEMP_MIN_CENTI 0
#define APP_TEMP_MAX_CENTI 6000

#define APP_TIME_MIN_S     6u
#define APP_TIME_MAX_S     5994u
#define APP_DELAY_MAX_S    300u

typedef enum {
    INC_IDLE, INC_RUNNING, INC_FINISHED, INC_DELAY,
    INC_COOL_REQUEST, INC_COOLING, INC_STORAGE, INC_ERROR
} inc_phase_t;

typedef struct {
    int32_t target_centi;           /* 用户液体目标温度，不是温控器原始 SV。 */
    uint32_t time_s;                /* 已设置的孵育时间，单位为秒。 */
    uint16_t delay_s;               /* 0 表示 N，不启用自动降温。 */
    inc_phase_t phase;
    uint32_t run_start_ms, run_duration_ms, run_delay_ms;
    uint32_t remaining_ms, delay_remaining_ms;
    uint32_t paused_total_ms, pause_start_ms; /* 暂停时间不计入本次孵育。 */
    uint8_t paused;
    uint8_t buzzer, cooling_request;
} incubator_model_t;

/** @brief 将模型恢复为默认设置并进入空闲阶段。 */
void incubator_model_init(incubator_model_t *m);
/** @brief 以给定的毫秒时间戳启动一次新的孵育运行。 */
int incubator_model_start(incubator_model_t *m, uint32_t now_ms);
/** @brief 根据当前时间推进倒计时和阶段转换。 */
void incubator_model_step(incubator_model_t *m, uint32_t now_ms);
/** @brief 清除蜂鸣标志，不改变运行计时。 */
void incubator_model_mute(incubator_model_t *m);
/* 冻结或继续倒计时，仅影响 RUNNING 和 DELAY 阶段。
 * 冻结期间不从设定孵育时间中扣除时间。 */
void incubator_model_set_paused(incubator_model_t *m, int paused, uint32_t now_ms);
/** @brief 取消运行并返回空闲/设置阶段。 */
void incubator_model_stop(incubator_model_t *m);
/** @brief 上报自动降温设定值写入结果。 */
void incubator_model_cooling_result(incubator_model_t *m, int success);
/** @brief 温度达到降温目标后更新保存状态。 */
void incubator_model_at_storage(incubator_model_t *m, int at_temperature);
/** @brief 判断模型是否处于运行或运行结束后的阶段。 */
int incubator_model_active(const incubator_model_t *m);
#endif
