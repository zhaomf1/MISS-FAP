$sourcePath = Join-Path $PSScriptRoot '..\Core\Src\app\ui.c'
$fontHeaderPath = Join-Path $PSScriptRoot '..\Core\Inc\app\ui_font.h'
$source = Get-Content -Raw -Encoding utf8 $sourcePath
$fontHeader = Get-Content -Raw -Encoding utf8 $fontHeaderPath

@(
    'UI_COL_PANEL',
    'UI_COL_CARD',
    'UI_COL_ORANGE',
    'UI_CARD_TEMP_X',
    'UI_CARD_DOOR_X',
    'ui_round_frame',
    'TXT_ALM',
    'TXT_HEAT_COOL',
    'TXT_TEMP_CAPTION',
    'TXT_OPEN_LID',
    'TXT_CLOSE_LID'
) | ForEach-Object {
    if ($source -notmatch $_) {
        throw "Reference-layout UI is missing required element: $_"
    }
}

if ($source -match 'focus_next|自动轮巡') {
    throw 'Reference-layout UI must not retain the demo auto-focus carousel.'
}

if ($source -notmatch 'ui_round_frame\(UI_PANEL_X, UI_PANEL_Y, UI_PANEL_W, UI_PANEL_H') {
    throw 'Reference-layout UI must draw the dark rounded main panel.'
}

if ($fontHeader -notmatch 'UI_FONT_CN16_N\s+42') {
    throw 'Reference-layout text requires the expanded 16px glyph set.'
}

Write-Output 'Reference-layout UI checks passed.'
