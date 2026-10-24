#ifndef TEMP_SERVICE_H
#define TEMP_SERVICE_H
#include <stdint.h>
typedef struct {
    int32_t pv_centi, applied_centi;
    uint32_t last_good_ms, completed_id;
    uint16_t alarm;
    uint8_t online, valid, target_valid, write_ok;
} temp_snapshot_t;
int temp_service_init(void);
/* One outstanding command is owned by the application service. */
int temp_service_set_target(int32_t liquid_centi, uint32_t id);
void temp_service_snapshot(temp_snapshot_t *out);
void temp_service_task(void *argument);
#endif
