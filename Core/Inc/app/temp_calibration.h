#ifndef TEMP_CALIBRATION_H
#define TEMP_CALIBRATION_H
#include <stdint.h>
/** @file temp_calibration.h
 *  @brief 温控器温度与液体温度之间的统一转换接口。
 */
/* 所有数值均为有符号 0.01℃。
 * 测量路径：温控器原始 PV -> 修正后的液体温度。
 * 控制路径：目标液体温度 -> 温控器 SV。
 * 后续取得标定数据后，只需替换这里的正向和逆向映射。
 * 界面和状态机中不能重复执行温度修正。 */
/** @brief 将温控器 PV 转换为界面显示的液体温度。 */
int32_t temp_calibration_to_display(int32_t module_centi);
/** @brief 将目标液体温度转换为温控器设定值。 */
int32_t temp_calibration_to_module(int32_t liquid_centi);
#endif
