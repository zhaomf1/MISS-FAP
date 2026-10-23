$root = Join-Path $PSScriptRoot '..'
$modbus = Get-Content -Raw -ErrorAction Stop (Join-Path $root 'Core/Src/common/modbus_rtu.c')
$port = Get-Content -Raw -ErrorAction Stop (Join-Path $root 'Core/Src/peripheral/rs485_port.c')
$portHeader = Get-Content -Raw -ErrorAction Stop (Join-Path $root 'Core/Inc/peripheral/rs485_port.h')

if ($modbus -match '#include\s+"usart\.h"') { throw 'Modbus protocol layer must not include usart.h.' }
if ($modbus -notmatch 'rs485_port_exchange') { throw 'Modbus protocol layer must use the RS485 port abstraction.' }
if ($portHeader -notmatch 'rs485_port_exchange') { throw 'RS485 port abstraction is missing exchange API.' }
if ($port -notmatch 'HAL_UART_Transmit') { throw 'RS485 port must own UART transmission.' }
if ($port -notmatch 'rs485_port_on_idle_irq') { throw 'RS485 port must own IDLE-frame completion handling.' }

Write-Output 'Modbus layering checks passed.'
