$motorPath = Join-Path $PSScriptRoot '..\Core\Src\app\motor.c'
$headerPath = Join-Path $PSScriptRoot '..\Core\Inc\app\motor.h'
$mainPath = Join-Path $PSScriptRoot '..\Core\Src\app\main.c'
$gpioPath = Join-Path $PSScriptRoot '..\Core\Src\peripheral\gpio.c'

$motor = Get-Content -Raw -Encoding utf8 $motorPath
$header = Get-Content -Raw -Encoding utf8 $headerPath
$main = Get-Content -Raw -Encoding utf8 $mainPath
$gpio = Get-Content -Raw -Encoding utf8 $gpioPath

if ($motor -notmatch '#define MOTOR_CMD_PORT\s+GPIOE') {
    throw 'Motor command port must be GPIOE.'
}

if ($motor -notmatch '#define MOTOR_CMD_PIN\s+GPIO_PIN_6') {
    throw 'Motor command pin must be GPIO_PIN_6.'
}

if ($motor -match '#define MOTOR_CMD_PORT\s+GPIOD|#define MOTOR_CMD_PIN\s+GPIO_PIN_14') {
    throw 'The old PD14 motor command route is still present.'
}

if ($header -notmatch '(?s)#define MOTOR_CMD_INVERTED\s+0') {
    throw 'VALVE5 must use its active-high polarity (MOTOR_CMD_INVERTED = 0).'
}

if ($header -notmatch '(?s)#define MOTOR_PE6_STATIC_HIGH_TEST\s+0') {
    throw 'PE6 static-high hardware test must be disabled for normal motor pulses.'
}

if ($motor -notmatch '(?s)HAL_GPIO_WritePin\(MOTOR_CMD_PORT, MOTOR_CMD_PIN, CMD_ACTIVE_LEVEL\);\s*osDelay\(width_ms\);\s*HAL_GPIO_WritePin\(MOTOR_CMD_PORT, MOTOR_CMD_PIN, CMD_IDLE_LEVEL\);') {
    throw 'Normal PE6 pulse generation is missing.'
}

if ($gpio -notmatch '(?s)HAL_GPIO_WritePin\(GPIOE,\s*GPIO_PIN_2\|GPIO_PIN_3\|GPIO_PIN_4\|GPIO_PIN_5\s*\|GPIO_PIN_6.*?GPIO_PIN_RESET\)') {
    throw 'PE6 is not initialized low by MX_GPIO_Init.'
}

if ($gpio -notmatch '(?s)GPIO_PIN_2\|GPIO_PIN_3\|GPIO_PIN_4\|GPIO_PIN_5\s*\|GPIO_PIN_6.*?GPIO_MODE_OUTPUT_PP') {
    throw 'PE6 is not configured as a push-pull output by MX_GPIO_Init.'
}

if ($main -match '电机指令线 PD14|GPIOD->BSRR\s*=\s*1UL\s*<<\s*\(14\s*\+\s*16\)') {
    throw 'Stale PD14 motor-command startup control remains in main.c.'
}

Write-Output 'Motor output-route checks passed.'
