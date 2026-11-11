#ifndef TEMP_SERVICE_H
#define TEMP_SERVICE_H
#include <stdint.h>

/** @file temp_service.h
 *  @brief 温控器异步轮询和设定值服务。
 */
typedef struct {
    int32_t pv_centi, applied_centi;
    uint32_t last_good_ms, completed_id;
    uint16_t alarm;
    /* ctrl_on: 温控器闭环开关状态。1 = 已成功下发 control=1(加热/制冷闭环运行),
     * 0 = 上电关闭或后续关闭。风扇等附属输出据此跟随温控开/关。 */
    uint8_t online, valid, target_valid, write_ok, ctrl_on;
} temp_snapshot_t;
/** @brief 创建命令队列并初始化服务快照。 */
int temp_service_init(void);
/** @brief 提交一次设定值写入，同时只允许一个事务处于未完成状态。 */
int temp_service_set_target(int32_t liquid_centi, uint32_t id);
/** @brief 复制最新温度和报警快照。 */
void temp_service_snapshot(temp_snapshot_t *out);
/** @brief 轮询温控器并完成排队的设定值事务。 */
void temp_service_task(void *argument);
#endif
