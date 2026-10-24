#ifndef TEMP_CALIBRATION_H
#define TEMP_CALIBRATION_H
#include <stdint.h>
/* All values are signed 0.01 degC.
 * Measurement path: raw module PV -> corrected liquid temperature.
 * Control path: desired liquid temperature -> module SV.
 * Replace both bodies with a calibrated mapping/inverse mapping later.
 * Never apply the correction a second time in UI or the state machine. */
int32_t temp_calibration_to_display(int32_t module_centi);
int32_t temp_calibration_to_module(int32_t liquid_centi);
#endif
