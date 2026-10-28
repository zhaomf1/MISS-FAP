$sourcePath = Join-Path $PSScriptRoot '..\Core\Src\app\ui_layout.c'
$source = Get-Content -Raw -Encoding utf8 $sourcePath

$required = @(
    'FOCUS_TEMPERATURE',
    'FOCUS_TIME',
    'FOCUS_DELAY',
    'create_temperature_area',
    'create_timing_area',
    'create_delay_area',
    'handle_temperature_key',
    'handle_time_key',
    'handle_delay_key',
    'activate_start_control',
    'activate_stop_control',
    'activate_door_control',
    'activate_mute_control',
    'render_temperature_setpoint',
    'render_temperature_widget',
    'render_header_door_status',
    'render_phase_indicator',
    'render_alarm_indicator',
    'render_time_widget',
    'render_delay_widget',
    'render_start_control',
    'render_stop_control',
    'render_door_control',
    'render_mute_control',
    'refresh_edit_cursor',
    'ICON_STOP_X',
    'ICON_DOOR_X',
    'ICON_MUTE_X'
)
foreach ($name in $required) {
    if ($source -notmatch [regex]::Escape($name)) {
        throw "UI layout structure is missing: $name"
    }
}

function Get-UiDefine([string]$Name) {
    $match = [regex]::Match($source, "#define\s+$Name\s+(\d+)")
    if (-not $match.Success) { throw "Missing UI layout constant: $Name" }
    return [int]$match.Groups[1].Value
}

$stop = @((Get-UiDefine 'ICON_STOP_X'), (Get-UiDefine 'ICON_STOP_Y'), (Get-UiDefine 'ACTION_ICON_SIZE'), (Get-UiDefine 'ACTION_ICON_SIZE'))
$door = @((Get-UiDefine 'ICON_DOOR_X'), (Get-UiDefine 'ICON_DOOR_Y'), 13, 11)
$mute = @((Get-UiDefine 'ICON_MUTE_X'), ((Get-UiDefine 'ICON_MUTE_Y') - 4), 11, 12)
$iconBoxes = @{ Stop = $stop; Door = $door; Mute = $mute }
$names = @($iconBoxes.Keys)
for ($i = 0; $i -lt $names.Count; $i++) {
    for ($j = $i + 1; $j -lt $names.Count; $j++) {
        $a = $iconBoxes[$names[$i]]; $b = $iconBoxes[$names[$j]]
        $overlap = ($a[0] -lt ($b[0] + $b[2])) -and (($a[0] + $a[2]) -gt $b[0]) -and
                   ($a[1] -lt ($b[1] + $b[3])) -and (($a[1] + $a[3]) -gt $b[1])
        if ($overlap) { throw "$($names[$i]) and $($names[$j]) icons overlap." }
    }
}

if ($source -notmatch 'lv_obj_add_flag\([^;]*LV_OBJ_FLAG_HIDDEN' -or
    $source -notmatch 'lv_obj_remove_flag\([^;]*LV_OBJ_FLAG_HIDDEN') {
    throw 'Action icons must support focus-based visibility.'
}

if ($source -notmatch 's_ic_stop\s*=\s*outline_rect\(screen,\s*ICON_STOP_X,\s*ICON_STOP_Y,\s*ACTION_ICON_SIZE') {
    throw 'Stop icon must remain a square outline.'
}
if ($source -notmatch 'int show_start\s*=\s*s_focus_index == FOCUS_START;' -or
    $source -notmatch 'int show_stop\s*=\s*s_focus_index == FOCUS_STOP;' -or
    $source -notmatch 'int show_door\s*=\s*s_focus_index == FOCUS_DOOR;' -or
    $source -notmatch 'int show_mute\s*=\s*s_focus_index == FOCUS_MUTE;') {
    throw 'Focused action icons must remain visible even when their actions are unavailable.'
}

if ($source -notmatch 's_focus_index\s*=\s*map\[s_focus_index\];' -or
    $source -match 'nav\(uint32_t key\)[\s\S]*?if\(allowed\(next\)\)') {
    throw 'Directional navigation must not skip controls based on action availability.'
}

if ($source -notmatch 'if\(!allowed\(s_focus_index\)\)[\s\S]*?unavailable in current state') {
    throw 'Unavailable controls must retain focus and reject OK activation.'
}

if ($source -notmatch 'if\(s_edit && !allowed\(s_focus_index\)\)[\s\S]*?s_edit = 0;') {
    throw 'A control becoming unavailable must exit editing without forcing focus to another control.'
}

$commandBindings = @(
    'handle_temperature_key[\s\S]*?app_service_submit\(APP_SET_TEMP',
    'handle_time_key[\s\S]*?app_service_submit\(APP_SET_TIME',
    'handle_delay_key[\s\S]*?app_service_submit\(APP_SET_DELAY',
    'activate_start_control[\s\S]*?app_service_submit\(APP_START',
    'activate_stop_control[\s\S]*?app_service_submit\(APP_STOP',
    'activate_door_control[\s\S]*?app_service_submit\(command, 0\)',
    'activate_mute_control[\s\S]*?app_service_submit\(APP_MUTE'
)
foreach ($binding in $commandBindings) {
    if ($source -notmatch $binding) { throw "UI control is not connected to its application command: $binding" }
}
$renderFunctions = [regex]::Matches($source, '(?s)static void render_[\s\S]*?(?=\nstatic void|\nvoid ui_layout_process)')
if (($renderFunctions | ForEach-Object { $_.Value }) -match 'app_service_submit') {
    throw 'A UI render function must not submit application or hardware commands.'
}

Write-Output 'UI layout structure checks passed.'

