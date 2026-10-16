$sourcePath = Join-Path $PSScriptRoot '..\Core\Src\app\motor.c'
$headerPath = Join-Path $PSScriptRoot '..\Core\Inc\app\motor.h'
$source = Get-Content -Raw -Encoding utf8 $sourcePath
$header = Get-Content -Raw -Encoding utf8 $headerPath

$initMatch = [regex]::Match($source, '(?s)static void motor_hw_init\(void\)\s*\{.*?(?=static void motor_pulse\(uint16_t width_ms\)\s*\{)')
if (-not $initMatch.Success) {
    throw 'Cannot locate motor input-capture initialization.'
}
$init = $initMatch.Value

if ($init -notmatch '(?s)ic\.ICPolarity\s*=\s*TIM_ICPOLARITY_FALLING;.*?HAL_TIM_IC_ConfigChannel\(&htim4, &ic, TIM_CHANNEL_1\)') {
    throw 'TIM4 CH1 must capture the falling edge that starts a feedback low pulse.'
}

if ($init -notmatch '(?s)ic\.ICPolarity\s*=\s*TIM_ICPOLARITY_RISING;.*?HAL_TIM_IC_ConfigChannel\(&htim4, &ic, TIM_CHANNEL_2\)') {
    throw 'TIM4 CH2 must capture the rising edge that ends a feedback low pulse.'
}

if ($source -notmatch '(?s)MOTOR_FB_MIN_VALID_US.*?MOTOR_FB_MAX_VALID_US') {
    throw 'Feedback low-pulse validity limits are missing.'
}

if ($source -notmatch 'item\.w_us\s*>=\s*MOTOR_FB_MIN_VALID_US\s*&&\s*item\.w_us\s*<=\s*MOTOR_FB_MAX_VALID_US') {
    throw 'Out-of-range feedback pulses are not filtered before queueing.'
}

Write-Output 'Motor feedback low-pulse capture checks passed.'
