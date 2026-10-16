/**
  ******************************************************************************
  * @file    lcd_st7789.c
  * @brief   ST7789V SPI LCD 驱动实现 (阻塞发送, 单一任务独占)
  *
  * 初始化序列来源: 中景园原厂例程 lcd_init.c (02-2.4IPS...STM32F103C8T6_SPI例程),
  * 2026-10-13 按 ZJY240S0800TG13 原厂序列替换 (原为通用序列占位)。
  * 原厂用 GPIO 模拟 SPI, 本工程用硬件 SPI1, 寄存器时序等价。
  ******************************************************************************
  */

#include "lcd_st7789.h"
#include <stdio.h>
#include "spi.h"      /* hspi1 */
#include "cmsis_os.h"

/* Private defines -----------------------------------------------------------*/
#define LCD_CS_PORT    GPIOA
#define LCD_CS_PIN     GPIO_PIN_4
#define LCD_DC_PORT    GPIOD
#define LCD_DC_PIN     GPIO_PIN_10
#define LCD_RST_PORT   GPIOD
#define LCD_RST_PIN    GPIO_PIN_11
#define LCD_BLK_PORT   GPIOD
#define LCD_BLK_PIN     GPIO_PIN_9

/* SPI 参数: 模式0(已在MX_SPI1_Init配好), 42MHz, 8bit, MSB first */
#define LCD_SPI_TIMEOUT_MS  100

/* Private variables ---------------------------------------------------------*/
static int s_inited = 0;

/* Private functions ---------------------------------------------------------*/

static void lcd_cs(int sel)   { HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, sel ? GPIO_PIN_RESET : GPIO_PIN_SET); }
static void lcd_dc(int data)  { HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, data ? GPIO_PIN_SET : GPIO_PIN_RESET); }

static void lcd_cmd(uint8_t c)
{
  lcd_cs(1);
  lcd_dc(0);
  HAL_SPI_Transmit(&hspi1, &c, 1, LCD_SPI_TIMEOUT_MS);
  lcd_cs(0);
}

static void lcd_data(const uint8_t *d, uint16_t n)
{
  if (n == 0)
    return;
  lcd_cs(1);
  lcd_dc(1);
  HAL_SPI_Transmit(&hspi1, (uint8_t *)d, n, LCD_SPI_TIMEOUT_MS);
  lcd_cs(0);
}

static void lcd_cmd_data(uint8_t c, const uint8_t *d, uint16_t n)
{
  lcd_cmd(c);
  lcd_data(d, n);
}

/**
  * @brief PA4 从 AF5(SPI1_NSS) 改回 GPIO 输出, 作软件片选
  */
static void lcd_pin_init(void)
{
  GPIO_InitTypeDef g = {0};

  HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
  g.Pin   = LCD_CS_PIN;
  g.Mode  = GPIO_MODE_OUTPUT_PP;
  g.Pull  = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
  HAL_GPIO_Init(LCD_CS_PORT, &g);
}

/**
  * @brief 设置写窗口 (行/列地址范围), 随后写数据自动递增并回绕
  */
static void lcd_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
  uint8_t d[4];

  d[0] = x0 >> 8; d[1] = x0; d[2] = x1 >> 8; d[3] = x1;
  lcd_cmd_data(0x2A, d, 4);                    /* CASET */
  d[0] = y0 >> 8; d[1] = y0; d[2] = y1 >> 8; d[3] = y1;
  lcd_cmd_data(0x2B, d, 4);                    /* RASET */
  lcd_cmd(0x2C);                               /* RAMWR */
}

/* ========================================================================== */
/* 对外接口                                                                     */
/* ========================================================================== */

void lcd_set_backlight(int on)
{
#if LCD_BLK_ACTIVE_HIGH
  HAL_GPIO_WritePin(LCD_BLK_PORT, LCD_BLK_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
  HAL_GPIO_WritePin(LCD_BLK_PORT, LCD_BLK_PIN, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
#endif
}

/**
  * @brief 寄存器初始化序列 (中景园 ZJY240S0800TG13 原厂例程, USE_HORIZONTAL=0 竖屏)
  */
static void lcd_init_regs(void)
{
  static const uint8_t madctl[]  = {0x00};     /* 竖屏 240x320 */
  static const uint8_t colmod[]  = {0x05};     /* 16bit RGB565 */
  static const uint8_t porctrl[] = {0x05, 0x05, 0x00, 0x33, 0x33};
  static const uint8_t gctrl[]   = {0x35};
  static const uint8_t vcoms[]   = {0x21};
  static const uint8_t lcmctrl[] = {0x2C};
  static const uint8_t vrhen[]   = {0x01};
  static const uint8_t vrhs[]    = {0x0B};
  static const uint8_t vdvs[]    = {0x20};
  static const uint8_t frctrl2[] = {0x0F};     /* 60Hz dot inversion */
  static const uint8_t pwrctrl1a[] = {0xA7, 0xA1};
  static const uint8_t pwrctrl1b[] = {0xA4, 0xA1};
  static const uint8_t regd6[]   = {0xA1};
  static const uint8_t gammap[]  = {0xD0, 0x04, 0x08, 0x0A, 0x09, 0x05, 0x2D,
                                    0x43, 0x49, 0x09, 0x16, 0x15, 0x26, 0x2B};
  static const uint8_t gamman[]  = {0xD0, 0x03, 0x09, 0x0A, 0x0A, 0x06, 0x2E,
                                    0x44, 0x40, 0x3A, 0x15, 0x15, 0x26, 0x2A};

  lcd_cmd_data(0x36, madctl, 1);               /* MADCTL */
  lcd_cmd_data(0x3A, colmod, 1);               /* COLMOD */
  lcd_cmd_data(0xB2, porctrl, 5);              /* PORCTRL */
  lcd_cmd_data(0xB7, gctrl, 1);                 /* GCTRL */
  lcd_cmd_data(0xBB, vcoms, 1);                 /* VCOMS */
  lcd_cmd_data(0xC0, lcmctrl, 1);              /* LCMCTRL */
  lcd_cmd_data(0xC2, vrhen, 1);                /* VDVVRHEN */
  lcd_cmd_data(0xC3, vrhs, 1);                 /* VRHS */
  lcd_cmd_data(0xC4, vdvs, 1);                 /* VDVS */
  lcd_cmd_data(0xC6, frctrl2, 1);              /* FRCTRL2 */
  lcd_cmd_data(0xD0, pwrctrl1a, 2);            /* PWRCTRL1 (原厂写两次) */
  lcd_cmd_data(0xD0, pwrctrl1b, 2);
  lcd_cmd_data(0xD6, regd6, 1);                 /* 原厂序列含此项 */
  lcd_cmd_data(0xE0, gammap, 14);              /* Gamma 正极性 */
  lcd_cmd_data(0xE1, gamman, 14);              /* Gamma 负极性 */
  lcd_cmd(0x21);                               /* INVON 反色 */
}

void lcd_init(void)
{
  if (s_inited)
    return;

  lcd_pin_init();
  lcd_set_backlight(0);

  /* 硬复位 */
  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
  osDelay(10);
  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_RESET);
  osDelay(10);
  HAL_GPIO_WritePin(LCD_RST_PORT, LCD_RST_PIN, GPIO_PIN_SET);
  osDelay(120);

  lcd_cmd(0x11);                               /* SLPOUT */
  osDelay(120);

  lcd_init_regs();

  lcd_cmd(0x29);                               /* DISPON */
  osDelay(50);

  lcd_set_backlight(1);
  s_inited = 1;
  printf("[lcd] init done\r\n");
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                   uint16_t color)
{
  uint32_t total = (uint32_t)w * h;
  uint32_t n;
  uint8_t  buf[2 * 256];                       /* 256px/块 缓冲 */
  uint16_t px = 256;
  uint16_t be_hi = (uint8_t)(color >> 8);      /* ST7789 高字节在前 */
  uint8_t  be_lo = (uint8_t)(color & 0xFF);
  uint16_t i;

  if (!s_inited)
    return;

  lcd_set_window(x, y, x + w - 1, y + h - 1);

  lcd_cs(1);
  lcd_dc(1);
  for (n = 0; n < total; n += px)
  {
    if (n + px > total)
      px = (uint16_t)(total - n);
    for (i = 0; i < px; i++)
    {
      buf[2 * i]     = be_hi;
      buf[2 * i + 1] = be_lo;
    }
    HAL_SPI_Transmit(&hspi1, buf, 2u * px, LCD_SPI_TIMEOUT_MS);
  }
  lcd_cs(0);
}

void lcd_fill_screen(uint16_t color)
{
  lcd_fill_rect(0, 0, LCD_W, LCD_H, color);
}

/**
  * @brief 画 RGB565 位图, MCU小端数据自动转大端; w*h*sizeof(uint16_t) 由调用方保证合法
  */
void lcd_blit(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
              const uint16_t *px)
{
  uint32_t total = (uint32_t)w * h;
  uint32_t n;
  uint16_t chunk_px = 256;
  uint8_t  buf[2 * 256];
  uint16_t i;

  if (!s_inited)
    return;

  lcd_set_window(x, y, x + w - 1, y + h - 1);

  lcd_cs(1);
  lcd_dc(1);
  for (n = 0; n < total; n += chunk_px)
  {
    if (n + chunk_px > total)
      chunk_px = (uint16_t)(total - n);
    for (i = 0; i < chunk_px; i++)
    {
      uint16_t c = px[n + i];
      buf[2 * i]     = (uint8_t)(c >> 8);      /* 小端转大端 */
      buf[2 * i + 1] = (uint8_t)(c & 0xFF);
    }
    HAL_SPI_Transmit(&hspi1, buf, 2u * chunk_px, LCD_SPI_TIMEOUT_MS);
  }
  lcd_cs(0);
}

/**
  * @brief CLI 'l': 初始化 + 8色条测试图 (驱动自检用)
  */
int lcd_test(void)
{
  static const uint16_t bars[] = {
    LCD_COLOR_RED, LCD_COLOR_GREEN, LCD_COLOR_BLUE, LCD_COLOR_WHITE,
    LCD_COLOR_GRAY, LCD_RGB565(255, 180, 0), LCD_RGB565(180, 0, 255),
    LCD_COLOR_BLACK,
  };
  int i;

  lcd_init();
  lcd_fill_screen(LCD_COLOR_DARKBG);
  for (i = 0; i < 8; i++)
    lcd_fill_rect(20, (uint16_t)(20 + i * 30), LCD_W - 40, 26, bars[i]);
  printf("[lcd] test pattern drawn (8 bars)\r\n");
  return 0;
}
