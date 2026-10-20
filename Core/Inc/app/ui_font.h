/**
  ******************************************************************************
  * @file    ui_font.h
  * @brief   UI 点阵字库接口 (数据由脚本自动生成于 ui_font.c)
  ******************************************************************************
  */
#ifndef UI_FONT_H
#define UI_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* 16x16 字形: 32字节, 行主序, 字节内MSB在前 */
typedef struct {
  uint16_t cp;            /* Unicode 码点 */
  uint8_t  bits[32];
} ui_glyph16_t;

/* 24x32 大字形: 96字节 */
typedef struct {
  uint16_t cp;
  uint8_t  bits[96];
} ui_glyph32_t;

extern const ui_glyph16_t ui_font_cn16[];
extern const ui_glyph32_t ui_font_big32[];

/* 表项数量 (与生成脚本一致, sizeof对extern不完整类型不可用) */
#define UI_FONT_CN16_N     30     /* 15中文 + 15ASCII */
#define UI_FONT_BIG32_N    14

#ifdef __cplusplus
}
#endif
#endif /* UI_FONT_H */
