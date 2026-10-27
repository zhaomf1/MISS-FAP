#include "temp_calibration.h"

/* 当前尚未提供标定系数，暂时使用原值映射，不人为增加偏移。 */
int32_t temp_calibration_to_display(int32_t module_centi) { return module_centi; }
int32_t temp_calibration_to_module(int32_t liquid_centi) { return liquid_centi; }
