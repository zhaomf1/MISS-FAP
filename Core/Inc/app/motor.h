/**
  ******************************************************************************
  * @file    motor.h
  * @brief   SND2506-24MM-305 电动翻盖驱动器 控制/反馈接口
  *
  * 硬件连接：
  *   PD13 <----> 电机 Feedback (空闲高电平, 低脉冲宽度=状态代码 0.8~7.5ms)
  *   PE6 ----> VALVE5 ----> 12V_OUT5 ----> 电机 Input
  *                (12V_IN 接 5V 时，输出高电平约为 5V；空闲低、单脉冲 10~90ms)
  *   电机线束(按控制板说明文件): 左->右 = Feedback、空、Input、GND、VCC(12V)
  *
  * PD13 复用为 TIM4_CH2 输入捕获 (1MHz 计数, 测低脉宽, ±0.3ms 窗口判定)
  * PE6 复用为 GPIO 输出，高电平使能 VALVE5。
  ******************************************************************************
  */
#ifndef MOTOR_H
#define MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ======================= 硬件配置（按实测调整） ======================= */

/* VALVE5 为高电平使能；保持 0，空闲=低、指令脉冲=高。 */
#define MOTOR_CMD_INVERTED          0

/* PE6 硬件测点自检：
 *  1 = MotorTask 初始化后固定输出高，所有脉冲命令不再改写 PE6；仅供量测。
 *  0 = 正常电机脉冲控制。
 * 测试时请断开电机 Input，或关闭电机供电，避免 VALVE5 持续导通造成误动作。 */
#define MOTOR_PE6_STATIC_HIGH_TEST  0

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
#define MOTOR_FB_FIRST_TIMEOUT_MS   2000   /* 指令后等首个反馈(2.5ms过程信号,
                                              单次)的时限——过程信号只在启动时
                                              发一次,之后巡航段静默属正常     */

/* Feedback 空闲为高电平；低电平脉冲宽度标称值 (us) 及判定窗口。
 * 原厂语义(2026-10-15 确认): 过程/到位信号为"单次脉冲"(动作启动/到达时各发一次,
 * 之后线静默空闲); 减速信号(3.3/5.0ms)在减速期间连续重复发送 */
#define MOTOR_FB_OPEN_DONE_US       800    /* 上翻到位, 单次                   */
#define MOTOR_FB_CLOSED_DONE_US     7500   /* 下翻到位, 单次                   */
#define MOTOR_FB_UP_DECEL_US        3300   /* 上翻减速, 减速期间连续           */
#define MOTOR_FB_DOWN_DECEL_US      5000   /* 下翻减速, 减速期间连续           */
#define MOTOR_FB_MOVING_US          2500   /* 运动启动, 单次(上/下共用,靠指令方向区分) */
#define MOTOR_FB_WIN_US             300    /* 窗口 ±300us (规格误差±0.1ms,留裕量) */
#define MOTOR_FB_MIN_VALID_US        (MOTOR_FB_OPEN_DONE_US - MOTOR_FB_WIN_US)
#define MOTOR_FB_MAX_VALID_US        (MOTOR_FB_CLOSED_DONE_US + MOTOR_FB_WIN_US)

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
  MOTOR_RET_BUSY,      /* 正在开/关动作中或命令队列占用         */
  MOTOR_RET_NOT_INIT,  /* 电机任务/命令队列尚未初始化          */
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
