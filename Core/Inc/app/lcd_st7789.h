/**
  ******************************************************************************
  * @file    lcd_st7789.h
  * @brief   2.4寸 240x320 SPI TFT (中景园 ZJY240S0800TG13, 控制器 ST7789V) 驱动
  *
  * 接口: SPI1 = SCK PB3 / MOSI PA7 (无MISO, 只写), 软件CS = PA4
  * 控制: 背光 BLK=PD9, 数据/指令 DC=PD10, 复位 RST=PD11
  *
  * 注意: CubeMX 把 PA4 配成了 AF5(SPI1_NSS)但 NSS=SOFT, 本驱动运行时改回
  *       GPIO 输出作软件片选; PD9/10/11 由 gpio.c 静态配置, 上电默认
  *       背光灭/RST拉低(屏幕保持复位), lcd_init() 时释放。
  *
  * 像素格式 RGB565, 16bit大端(高字节在前)写入。仅允许单一任务调用(UI任务)。
  ******************************************************************************
  */
#ifndef LCD_ST7789_H
#define LCD_ST7789_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#define LCD_W   320     /* 横屏(2026-10-20): MADCTL=0x60 转90度 */
#define LCD_H   240
#define LCD_MADCTL_VAL   0x60    /* 0x00=竖屏 0x60=横屏(转90度) 0xA0=反向横屏 */

/* 背光极性: 1=高电平点亮 */
#define LCD_BLK_ACTIVE_HIGH   1

/* RGB565 颜色合成 */
#define LCD_RGB565(r, g, b) \
  (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))

/* 常用色 */
#define LCD_COLOR_BLACK  0x0000
#define LCD_COLOR_WHITE  0xFFFF
#define LCD_COLOR_RED    LCD_RGB565(255,  60,  60)
#define LCD_COLOR_GREEN  LCD_RGB565(  0, 200,  60)
#define LCD_COLOR_BLUE   LCD_RGB565( 40, 130, 255)
#define LCD_COLOR_GRAY   LCD_RGB565(120, 120, 130)
#define LCD_COLOR_DARKBG LCD_RGB565( 22,  26,  38)   /* 效果图深蓝灰底色 */

void lcd_init(void);                       /* 上电初始化(含复位/寄存器/开显示/开背光) */
void lcd_set_backlight(int on);
void lcd_fill_screen(uint16_t color);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t color);
void lcd_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
              const uint16_t *px);         /* 画RGB565位图(小端数据自动换字节序) */
int  lcd_test(void);                        /* CLI 'l': 初始化+彩色测试图 */

#ifdef __cplusplus
}
#endif
#endif /* LCD_ST7789_H */
