/**
  ******************************************************************************
  * @file    ui.c
  * @brief   主界面 UI 演示版 (横屏 320x240)
  *
  * 布局 (2026-10-20 横向排布):
  *   +0    顶部系统状态栏(全宽, 只显示)
  *   +48   温度(左) | 时间(右)      <- 可聚焦=可编辑
  *   +152  开舱门(左) | 关舱门(右)   <- 可聚焦=可触发
  *
  * 演示版: 焦点框每 800ms 自动轮巡, 时间走秒(定时任务)。按键/电机未接入。
  ******************************************************************************
  */

#include "ui.h"
#include "ui_font.h"
#include <string.h>
#include <stdio.h>
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "lcd_st7789.h"

/* ---------------- 颜色 ---------------- */
#define UI_COL_SCREEN_BG   LCD_RGB565( 18,  22,  34)   /* 屏幕底色       */
#define UI_COL_AREA_BG     LCD_RGB565( 30,  36,  52)   /* 区域底色       */
#define UI_COL_AREA_BORDER LCD_RGB565( 74,  84, 110)   /* 区域普通边框   */
#define UI_COL_FG          LCD_RGB565(235, 238, 245)   /* 主文字         */
#define UI_COL_DIM         LCD_RGB565(140, 150, 165)   /* 次文字         */
#define UI_COL_FOCUS_EDIT  LCD_RGB565(255, 150,  40)   /* 焦点框-可编辑   */
#define UI_COL_FOCUS_ACT   LCD_RGB565(255, 220,   0)   /* 焦点框-可触发   */
#define UI_COL_OPEN        LCD_RGB565( 70, 210, 110)   /* 开舱门主题色    */
#define UI_COL_CLOSE       LCD_RGB565(240,  90,  80)   /* 关舱门主题色    */
#define UI_COL_STATUS_OK   LCD_RGB565( 90, 220, 130)   /* 状态正常       */

/* ---------------- 布局坐标 (320x240 横屏, 横向排布) ---------------- */
typedef struct {
  uint16_t x, y, w, h;
} ui_area_t;

#define UI_AREA_N        4     /* 可聚焦功能区域数 */

static const ui_area_t s_area[UI_AREA_N] = {
  {   4,  48, 152,  92 },   /* 温度   (左上) */
  { 164,  48, 152,  92 },   /* 时间   (右上) */
  {   4, 152, 152,  84 },   /* 开舱门 (左下) */
  { 164, 152, 152,  84 },   /* 关舱门 (右下) */
};

/* 顶部状态栏 (只显示, 不可聚焦) */
#define UI_STATUS_X      4
#define UI_STATUS_Y      4
#define UI_STATUS_W    312
#define UI_STATUS_H     36

/* 区域内文字位置 */
#define UI_PAD_X         12    /* 区域内文字左边距           */
#define UI_LABEL_DY       8    /* 标签相对区域顶 y          */
#define UI_BIG_DY         40   /* 大数字相对区域顶 y         */

/* ---------------- 文案 (Unicode 码点) ---------------- */
static const uint16_t TXT_STATUS[]  = {0x7CFB, 0x7EDF, 0x72B6, 0x6001};  /* 系统状态 */
static const uint16_t TXT_RUNNING[] = {0x8FD0, 0x884C, 0x4E2D};          /* 运行中   */
static const uint16_t TXT_TEMP[]    = {0x6E29, 0x5EA6};                  /* 温度     */
static const uint16_t TXT_TIME[]    = {0x65F6, 0x95F4};                  /* 时间     */
static const uint16_t TXT_OPEN[]    = {0x5F00, 0x8231, 0x95E8};          /* 开舱门   */
static const uint16_t TXT_CLOSE[]   = {0x5173, 0x8231, 0x95E8};          /* 关舱门   */

/* ---------------- 绘制原语 ---------------- */

static uint16_t s_glyph_px[32 * 32];    /* 单任务使用, 无重入 */

static const ui_glyph16_t *ui_find16(uint16_t cp)
{
  uint32_t i;

  for (i = 0; i < UI_FONT_CN16_N; i++)
    if (ui_font_cn16[i].cp == cp)
      return &ui_font_cn16[i];
  return NULL;
}

static const ui_glyph32_t *ui_find32(uint16_t cp)
{
  uint32_t i;

  for (i = 0; i < UI_FONT_BIG32_N; i++)
    if (ui_font_big32[i].cp == cp)
      return &ui_font_big32[i];
  return NULL;
}

/**
  * @brief  画一个 16x16 字形 (带底色, 不透明)
  */
static void ui_blit16(uint16_t x, uint16_t y, uint16_t cp,
                      uint16_t fg, uint16_t bg)
{
  const ui_glyph16_t *g = ui_find16(cp);
  uint32_t r, c;

  if (g == NULL)
    return;

  for (r = 0; r < 16; r++)
    for (c = 0; c < 16; c++)
      s_glyph_px[r * 16 + c] =
        (g->bits[r * 2 + c / 8] >> (7 - (c % 8))) & 1 ? fg : bg;

  lcd_blit(x, y, 16, 16, s_glyph_px);
}

/**
  * @brief  画一串 16px 文本 (Unicode 码点数组), 每字前进16px
  */
static void ui_text16(uint16_t x, uint16_t y, const uint16_t *txt, uint32_t n,
                      uint16_t fg, uint16_t bg)
{
  uint32_t i;

  for (i = 0; i < n; i++)
    ui_blit16((uint16_t)(x + i * 16), y, txt[i], fg, bg);
}

/**
  * @brief  画一个 24x32 大字形 (大数字/时间), 每字前进24px
  */
static void ui_big32(uint16_t x, uint16_t y, uint16_t cp,
                      uint16_t fg, uint16_t bg)
{
  const ui_glyph32_t *g = ui_find32(cp);
  uint32_t r, c;

  if (g == NULL)
    return;

  for (r = 0; r < 32; r++)
    for (c = 0; c < 24; c++)
      s_glyph_px[r * 24 + c] =
        (g->bits[r * 3 + c / 8] >> (7 - (c % 8))) & 1 ? fg : bg;

  lcd_blit(x, y, 24, 32, s_glyph_px);
}

/**
  * @brief  画 24x32 数字串(如 "37.5℃" "00:12")
  */
static void ui_big_str(uint16_t x, uint16_t y, const uint16_t *s, uint32_t n,
                        uint16_t fg, uint16_t bg)
{
  uint32_t i;

  for (i = 0; i < n; i++)
    ui_big32((uint16_t)(x + i * 24), y, s[i], fg, bg);
}

/**
  * @brief  画矩形边框 (t 像素宽)
  */
static void ui_frame(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                      uint16_t t, uint16_t color)
{
  lcd_fill_rect(x, y, w, t, color);                      /* 上 */
  lcd_fill_rect(x, (uint16_t)(y + h - t), w, t, color);   /* 下 */
  lcd_fill_rect(x, (uint16_t)(y + t), t, (uint16_t)(h - 2 * t), color);  /* 左 */
  lcd_fill_rect((uint16_t)(x + w - t), (uint16_t)(y + t), t,
                 (uint16_t)(h - 2 * t), color);           /* 右 */
}

/* ---------------- 整屏静态绘制 ---------------- */

static void ui_draw_static(void)
{
  const ui_area_t *a;
  uint16_t txt[8];

  lcd_fill_screen(UI_COL_SCREEN_BG);

  /* 顶部系统状态栏: "系统状态: 运行中" */
  lcd_fill_rect(UI_STATUS_X, UI_STATUS_Y, UI_STATUS_W, UI_STATUS_H,
                 UI_COL_AREA_BG);
  ui_frame(UI_STATUS_X, UI_STATUS_Y, UI_STATUS_W, UI_STATUS_H, 2,
           UI_COL_AREA_BORDER);
  ui_text16((uint16_t)(UI_STATUS_X + 12), (uint16_t)(UI_STATUS_Y + 10),
            TXT_STATUS, 4, UI_COL_DIM, UI_COL_AREA_BG);
  txt[0] = ':'; txt[1] = ' ';
  ui_text16((uint16_t)(UI_STATUS_X + 12 + 4 * 16),
             (uint16_t)(UI_STATUS_Y + 10), txt, 2, UI_COL_DIM, UI_COL_AREA_BG);
  ui_text16((uint16_t)(UI_STATUS_X + 12 + 6 * 16),
             (uint16_t)(UI_STATUS_Y + 10), TXT_RUNNING, 3,
             UI_COL_STATUS_OK, UI_COL_AREA_BG);

  /* 温度 (左上) */
  a = &s_area[0];
  lcd_fill_rect(a->x, a->y, a->w, a->h, UI_COL_AREA_BG);
  ui_text16((uint16_t)(a->x + UI_PAD_X), (uint16_t)(a->y + UI_LABEL_DY),
            TXT_TEMP, 2, UI_COL_DIM, UI_COL_AREA_BG);
  txt[0] = '3'; txt[1] = '7'; txt[2] = '.'; txt[3] = '5'; txt[4] = 0x2103;
  ui_big_str((uint16_t)(a->x + UI_PAD_X), (uint16_t)(a->y + UI_BIG_DY),
              txt, 5, UI_COL_FG, UI_COL_AREA_BG);

  /* 时间 (右上) */
  a = &s_area[1];
  lcd_fill_rect(a->x, a->y, a->w, a->h, UI_COL_AREA_BG);
  ui_text16((uint16_t)(a->x + UI_PAD_X), (uint16_t)(a->y + UI_LABEL_DY),
            TXT_TIME, 2, UI_COL_DIM, UI_COL_AREA_BG);
  txt[0] = '0'; txt[1] = '0'; txt[2] = ':'; txt[3] = '0'; txt[4] = '0';
  ui_big_str((uint16_t)(a->x + UI_PAD_X), (uint16_t)(a->y + UI_BIG_DY),
              txt, 5, UI_COL_FG, UI_COL_AREA_BG);

  /* 开舱门 (左下) / 关舱门 (右下): 3字48px 居中 */
  a = &s_area[2];
  lcd_fill_rect(a->x, a->y, a->w, a->h, UI_COL_AREA_BG);
  ui_text16((uint16_t)(a->x + (a->w - 48) / 2),
             (uint16_t)(a->y + (a->h - 16) / 2),
             TXT_OPEN, 3, UI_COL_OPEN, UI_COL_AREA_BG);
  a = &s_area[3];
  lcd_fill_rect(a->x, a->y, a->w, a->h, UI_COL_AREA_BG);
  ui_text16((uint16_t)(a->x + (a->w - 48) / 2),
             (uint16_t)(a->y + (a->h - 16) / 2),
             TXT_CLOSE, 3, UI_COL_CLOSE, UI_COL_AREA_BG);

  /* 区域普通边框最后画(焦点框会覆盖) */
  for (uint32_t i = 0; i < UI_AREA_N; i++)
    ui_frame(s_area[i].x, s_area[i].y, s_area[i].w, s_area[i].h, 2,
             UI_COL_AREA_BORDER);
}

/* ---------------- 焦点与动态刷新 ---------------- */

static void ui_focus_draw(uint32_t idx, int on)
{
  const ui_area_t *a = &s_area[idx];
  uint16_t col;

  if (idx < 2)
    col = on ? UI_COL_FOCUS_EDIT : UI_COL_AREA_BORDER;  /* 温度/时间=可编辑 */
  else
    col = on ? UI_COL_FOCUS_ACT : UI_COL_AREA_BORDER;   /* 开/关键=可触发 */

  ui_frame(a->x, a->y, a->w, a->h, 2, col);
}

/**
  * @brief  刷新时间显示 (走秒定时任务演示, MM:SS)
  */
static void ui_time_show(uint32_t sec)
{
  uint16_t txt[5];

  txt[0] = (uint16_t)('0' + (sec / 60 / 10) % 10);
  txt[1] = (uint16_t)('0' + (sec / 60) % 10);
  txt[2] = ':';
  txt[3] = (uint16_t)('0' + (sec % 60 / 10));
  txt[4] = (uint16_t)('0' + (sec % 10));
  ui_big_str((uint16_t)(s_area[1].x + UI_PAD_X),
              (uint16_t)(s_area[1].y + UI_BIG_DY),
              txt, 5, UI_COL_FG, UI_COL_AREA_BG);
}

/* ========================================================================== */
/* UI 任务 (演示版)                                                             */
/* ========================================================================== */

void UiTask(void *argument)
{
  (void)argument;
  TickType_t now;
  TickType_t sec_next, focus_next;
  uint32_t  sec = 0;
  uint32_t  focus = 0;

  lcd_init();
  ui_draw_static();
  ui_focus_draw(focus, 1);
  ui_time_show(sec);

  now = xTaskGetTickCount();
  sec_next   = now + pdMS_TO_TICKS(1000);   /* 走秒 */
  focus_next = now + pdMS_TO_TICKS(800);     /* 焦点自动轮巡(演示) */

  printf("[ui] demo start (landscape 320x240)\r\n");

  for (;;)
  {
    now = xTaskGetTickCount();

    if ((int32_t)(now - sec_next) >= 0)
    {
      sec_next += pdMS_TO_TICKS(1000);
      sec++;
      ui_time_show(sec);
    }

    if ((int32_t)(now - focus_next) >= 0)
    {
      focus_next += pdMS_TO_TICKS(800);
      ui_focus_draw(focus, 0);
      focus = (focus + 1) % UI_AREA_N;
      ui_focus_draw(focus, 1);
    }

    osDelay(50);
  }
}
