/**
  ******************************************************************************
  * @file    motor.h
  * @brief   SND2506-24MM-305 电动翻盖驱动器 控制/反馈接口
  *
  * 硬件连接（飞线）：
  *   PD13 <----> 电机 Feedback (实测2026-10-12: 空闲低电平, 每~34ms一个高脉冲,
  *                高脉冲宽度=状态代码 0.8~7.5ms, 与规格书"低电平"描述相反)
  *   PD14 ----> 电机 Input   (标称5V, 空闲低, 高电平单脉冲, 10~90ms)
  *   电机 Pin1=12V, Pin2=GND, Pin5=Swio 不接
  *
  * PD13 复用为 TIM4_CH2 输入捕获 (1MHz 计数, 测高脉宽, ±0.3ms 窗口判定)
  * PD14 复用为 GPIO 输出, 极性由 MOTOR_CMD_INVERTED 决定 (见 motor.c)
  ******************************************************************************
  */
#ifndef MOTOR_H
#define MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ======================= 硬件配置（按实测调整） ======================= */

/* 输出极性方案:
 *  0 = 方案C: PD14 推挽 3.3V 直推, 空闲=低, 脉冲=高          (零硬件)
 *  1 = 方案A: PD14 开漏经 2N7002 反相, 空闲=高(MOS导通->Input=0V),
 *             脉冲=拉低(MOS截止->Input 被10k上拉到5V)          (1管+1电阻) */
#define MOTOR_CMD_INVERTED          0

/* PD13 内部上拉: 反馈开漏时需要; 反馈推挽时无害(5V信号经40k弱上拉仅~40uA) */
#define MOTOR_FB_PULLUP             1

/* 安装方式: 左装=90ms / 右装=40ms (每次电机上电必须收到一次, 否则升降指令可能无效) */
#define MOTOR_SIDE                  MOTOR_SIDE_LEFT

/* 上电是否自动发安装指令(电机自校准会转动):
 *  1 = 上电自动安装 (自动台架流程)
 *  0 = 手动模式: 上电不动, 由串口发 i40/i90 安装, i10~i70 正反转测试 */
#define MOTOR_AUTO_INSTALL          0

/* 上电后等电机自身MCU启动的时间 (实测再调) */
#define MOTOR_STARTUP_DELAY_MS      500

/* 各种超时 (ms) */
#define MOTOR_INSTALL_TIMEOUT_MS    15000  /* 发安装指令后等"下翻到位"的时限   */
#define MOTOR_MOVE_TIMEOUT_MS       20000  /* 开/关动作整体完成时限            */
#define MOTOR_FB_MOTION_LOST_MS     200    /* 运动中无反馈脉冲 -> 判定断线     */

/* 反馈低脉宽标称值 (us) 及判定窗口 */
#define MOTOR_FB_OPEN_DONE_US       800    /* 上翻到位                         */
#define MOTOR_FB_CLOSED_DONE_US     7500   /* 下翻到位                         */
#define MOTOR_FB_UP_DECEL_US        3300   /* 上翻减速                         */
#define MOTOR_FB_DOWN_DECEL_US      5000   /* 下翻减速                         */
#define MOTOR_FB_MOVING_US          2500   /* 过程(上/下翻共用,靠指令方向区分) */
#define MOTOR_FB_WIN_US             300    /* 窗口 ±300us (规格误差±0.1ms,留裕量) */

/* 指令脉宽 (ms) */
#define MOTOR_PULSE_UP_1            10
#define MOTOR_PULSE_UP_2            20
#define MOTOR_PULSE_UP_3            30
#define MOTOR_PULSE_DOWN_1          50
#define MOTOR_PULSE_DOWN_2          60
#define MOTOR_PULSE_DOWN_3          70
#define MOTOR_PULSE_INSTALL_LEFT    90
#define MOTOR_PULSE_INSTALL_RIGHT   40

/* ======================= API ======================= */

typedef enum {
  MOTOR_UNINIT = 0,
  MOTOR_INSTALLING,   /* 已发安装方式指令, 等下翻到位反馈       */
  MOTOR_OPENING,
  MOTOR_OPEN,
  MOTOR_CLOSING,
  MOTOR_CLOSED,
  MOTOR_FAULT,        /* 超时/无反馈, 需 motor_reinit() 恢复   */
} motor_state_t;

typedef enum { MOTOR_SPEED_1 = 1, MOTOR_SPEED_2, MOTOR_SPEED_3 } motor_speed_t;
typedef enum { MOTOR_SIDE_RIGHT = 0, MOTOR_SIDE_LEFT } motor_side_t;

typedef enum {
  MOTOR_RET_OK = 0,
  MOTOR_RET_BUSY,      /* 正在安装/开/关动作中                  */
  MOTOR_RET_NOT_INIT,  /* 尚未完成安装方式初始化                */
  MOTOR_RET_FAULT,
  MOTOR_RET_ERR,
} motor_ret_t;

/* 由 freertos.c 里 MX_FREERTOS_Init 的 USER CODE 段创建 */
void MotorTask(void *argument);
void CliTask(void *argument);

/* ---- 应用接口 ---- */
motor_state_t motor_get_state(void);
const char   *motor_state_str(motor_state_t s);
motor_ret_t   motor_open(motor_speed_t speed);        /* 开盖 */
motor_ret_t   motor_close(motor_speed_t speed);      /* 关盖 */
motor_ret_t   motor_reinit(void);                     /* 重发安装指令(恢复FAULT) */
motor_ret_t   motor_send_raw_pulse(uint16_t width_ms);/* 调试: 绕过状态机发原始脉冲 */

#ifdef __cplusplus
}
#endif
#endif /* MOTOR_H */
