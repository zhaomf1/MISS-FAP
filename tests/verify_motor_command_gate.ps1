$sourcePath = Join-Path $PSScriptRoot '..\Core\Src\app\motor.c'
$source = Get-Content -Raw -Encoding utf8 $sourcePath

$start = $source.IndexOf('static motor_ret_t send_cmd')
$end = $source.IndexOf('motor_ret_t motor_open', $start)
if ($start -lt 0 -or $end -lt 0) {
    throw 'Cannot locate send_cmd implementation.'
}
$sendCmd = $source.Substring($start, $end - $start)

if ($sendCmd -match 'MOTOR_UNINIT|MOTOR_INSTALLING|MOTOR_OPENING|MOTOR_CLOSING|MOTOR_FAULT') {
    throw 'o/c commands still depend on motor state instead of directly queuing a pulse.'
}

if ($sendCmd -notmatch 'xQueueSend\(s_cmd_q, &cmd, 0\)') {
    throw 'Commands must still enter the MotorTask queue for serialized pulse output.'
}

Write-Output 'Motor command direct-queue checks passed.'
