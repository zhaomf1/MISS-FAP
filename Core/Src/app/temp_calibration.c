#include "temp_calibration.h"

/* Identity until calibration coefficients are supplied. No fabricated offset. */
int32_t temp_calibration_to_display(int32_t module_centi) { return module_centi; }
int32_t temp_calibration_to_module(int32_t liquid_centi) { return liquid_centi; }
