$sourcePath = Join-Path $PSScriptRoot '..\Core\Src\app\motor.c'
$source = Get-Content -Raw -Encoding utf8 $sourcePath

if ($source -notmatch 'static volatile uint32_t\s+s_fb_capture_count') {
    throw 'PD13 capture counter for live diagnostics is missing.'
}

if ($source -notmatch 'static void cli_diag_toggle\(void\)') {
    throw 'The d-command live diagnostic toggle is missing.'
}

if ($source -notmatch "case 'd':\s*cli_diag_toggle\(\);") {
    throw 'The d command does not start/stop the live PD13 diagnostic.'
}

if ($source -notmatch 'HAL_GPIO_ReadPin\(MOTOR_FB_PORT, MOTOR_FB_PIN\)') {
    throw 'Live diagnostic does not print the instantaneous PD13 level.'
}

if ($source -notmatch '\[diag\] state=') {
    throw 'Live diagnostic state output is missing.'
}

Write-Output 'Motor feedback-monitor checks passed.'
