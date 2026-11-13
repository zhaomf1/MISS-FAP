#include "temp_calibration.h"

/*
 * 夏繁制冷器(温控模块)温度换算 —— 严格按标定公式换算, 不做任何温度范围夹紧。
 *
 * 模块内部(SV 与 PV)使用它自己的刻度 y, 界面统一使用真实温度 x(0.01℃):
 *     x = 0.8501 × y + 2.7285      (读 PV 显示用)
 *     y = (x − 2.7285) / 0.8501    (写 SV 用)
 *
 * 这两个十进制小数都可以精确表示为整数比, 所以用整数运算即等于公式本身:
 *     0.8501 = 8501 / 10000        (TEMP_CAL_K / 10000)
 *     2.7285 = 27285 / 10000       (TEMP_CAL_B / 10000)
 * 换算到 0.01℃(centi)单位后:
 *     to_display: x = (8501·y + 272.85·10^4) / 10^4
 *     to_module : y = (10000·x − 272.85·10^4) / 8501
 *
 * 中间量用 int64, 因此任意 int32 输入都不会溢出, 负温度(0℃以下)同样适用;
 * 只有寄存器本身是 16 位有符号, 那是协议宽度限制, 不在这里处理。
 */

#define TEMP_CAL_K        8501     /* 0.8501 × 10^4 */
#define TEMP_CAL_B_CENTI  2728500  /* 2.7285℃ × 10^4, 已折算成 0.01℃ 单位 */

/* 四舍五入(远离零), 保证正负温度取整一致, 避免负值向零偏 1 centi */
static int32_t div_round_i64(int64_t numerator, int64_t denominator)
{
    int64_t result;

    if(numerator >= 0) result = (numerator + denominator / 2) / denominator;
    else               result = -((-numerator + denominator / 2) / denominator);

    /* 只防 int32 类型越界(不是温度限幅): 极端输入时饱和, 避免回绕成反号 */
    if(result > 2147483647LL)  return 2147483647;
    if(result < -2147483648LL) return (-2147483647 - 1);
    return (int32_t)result;
}

/* 温控器原始 PV(y, 0.01℃) -> 界面真实温度(x, 0.01℃) */
int32_t temp_calibration_to_display(int32_t module_centi)
{
    return div_round_i64((int64_t)TEMP_CAL_K * module_centi + TEMP_CAL_B_CENTI,
                         10000);
}

/* 界面真实温度(x, 0.01℃) -> 写入温控器的 SV(y, 0.01℃) */
int32_t temp_calibration_to_module(int32_t liquid_centi)
{
    return div_round_i64((int64_t)liquid_centi * 10000 - TEMP_CAL_B_CENTI,
                         TEMP_CAL_K);
}
