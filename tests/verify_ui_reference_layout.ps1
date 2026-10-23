$sourcePath = Join-Path $PSScriptRoot '..\Core\Src\app\ui_ascii.c'
$source = Get-Content -Raw -Encoding utf8 $sourcePath

@(
    'UI_COL_BG',
    'UI_COL_CARD',
    'UI_COL_BLUE',
    'UI_CARD_TEMP_X',
    'UI_CARD_DOOR_X',
    'UI_CARD_TEMP_X',
    'UI_CARD_DOOR_X',
    'ui_init',
    'ui_key_event',
    '温度',
    '孵育时间',
    '延迟转4℃'
) | ForEach-Object {
    if ($source -notmatch $_) {
        throw "Reference-layout UI is missing required element: $_"
    }
}

if ($source -match 'focus_next|自动轮巡') {
    throw 'Reference-layout UI must not retain the demo auto-focus carousel.'
}

Write-Output 'Reference-layout UI checks passed.'
