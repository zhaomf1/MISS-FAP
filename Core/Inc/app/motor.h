/**
  ******************************************************************************
  * @file    motor.h
  * @brief   SND2506-24MM-305 电动翻盖驱动器 驱动接口（纯驱动版，无测试代码）
  *
  * 硬件连接（2026-10-19 实测定稿，测试板）：
  *   PD13 ----> 电机 Input   (推挽3.3V直推, 空闲低+高电平单脉冲10~90ms)
  *   PD14 <----> 电机 Feedback (空闲高电平, 低脉冲宽度=状态码, 持续广播)
  *   电机线束(按厂商控制板说明, 左->右): Feedback、空、Input、GND、VCC(12V)
  *   PE6 ----> VALVE_5 输出级 ----> 电机12V供电开关 (main.c 上电拉高, 系统级)
  *
  * PD14 复用为 TIM4_CH3 输入捕获 (1MHz, CH3/CH4双沿测低脉宽)
  * PD13 复用为 GPIO 输出, 极性由 MOTOR_CMD_INVERTED 决定
  *
  * 使用流程 (应用层):
  *   1. 上电后 MotorTask 自动完成硬件初始化 (状态=UNINIT)
  *   2. motor_init()   —— 发安装方式指令(i90), 电机自校准, 到位后状态=CLOSED/OPEN
  *   3. motor_open()/motor_close() —— 上翻/下翻(1~3档), 到位自动停
  *   4. motor_get_state() 查询状态; FAULT 后重新 motor_init() 恢复
  *
  * 信号语义 (2026-10-19 实测+文档确认):
  *   到位信号(0.8/7.5ms)到位后持续广播(心跳)——状态机只消费这两档
  *   过程/减速信号(2.5/5.8/3.3/5.0ms)仅运动阶段出现, 驱动不消费
  *   指令后2s仍收到旧位置心跳 = 电机忙窗口没理会指令(保压/自校准期间)
  ******************************************************************************
  */
#ifndef MOTOR_H
#define MOTOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* ======================= 硬件配置 ======================= */

/* 指令极性: 0=推挽直推(空闲低+高脉冲) 1=开漏经2N7002反相(预留) */
#define MOTOR_CMD_INVERTED          0

/* PD14 内部上拉: 反馈开漏时提供空闲高; 推挽时无害 */
#define MOTOR_FB_PULLUP             1

/* 安装方式: 左装=90ms / 右装=40ms (每次电机上电必须收到一次, 否则升降可能无效) */
#define MOTOR_SIDE                  MOTOR_SIDE_LEFT

/* 上电后等电机自身MCU启动的时间 */
#define MOTOR_STARTUP_DELAY_MS      500

/* 各种超时 (ms) */
#define MOTOR_INSTALL_TIMEOUT_MS    15000  /* 安装指令后等"到位"的时限            */
#define MOTOR_INSTALL_MIN_MS        5000   /* i90后至少等这么久才认"到位"——到位
                                              心跳是持续广播, 校准期间趴在关位的
                                              7.5心跳不代表校准完成              */
#define MOTOR_MOVE_TIMEOUT_MS        20000  /* 开/关动作整体完成时限               */
#define MOTOR_FB_FIRST_TIMEOUT_MS    2000   /* 指令后旧位置心跳持续超此时长 =
                                              电机没理会指令(忙窗口), 报FAULT      */

/* 反馈脉冲宽度标称值 (us) 及判定窗口 (文档+实测一致, ±0.1ms规格)
 * 驱动只消费两个到位心跳; 过程/减速码不参与逻辑, 仅备查 */
#define MOTOR_FB_OPEN_DONE_US       800    /* 上翻到位心跳 (状态机消费) */
#define MOTOR_FB_CLOSED_DONE_US     7500   /* 下翻到位心跳 (状态机消费) */
#define MOTOR_FB_UP_DECEL_US        3300   /* 上翻减速 (备查)            */
#define MOTOR_FB_DOWN_DECEL_US      5000   /* 下翻减速 (备查)            */
#define MOTOR_FB_MOVING_US          2500   /* 上翻过程 (备查)            */
#define MOTOR_FB_DOWN_MOVING_US     5800   /* 下翻过程 (备查, 实测值)     */
#define MOTOR_FB_WIN_US             300    /* 窗口 ±300us                */

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
  MOTOR_UNINIT = 0,    /* 硬件就绪, 电机尚未安装初始化        */
  MOTOR_INSTALLING,   /* 已发安装指令, 等到位                 */
  MOTOR_OPENING,
  MOTOR_OPEN,
  MOTOR_CLOSING,
  MOTOR_CLOSED,
  MOTOR_FAULT,        /* 超时/电机忙未恢复, 需 motor_init()   */
} motor_state_t;

typedef enum { MOTOR_SPEED_1 = 1, MOTOR_SPEED_2, MOTOR_SPEED_3 } motor_speed_t;
typedef enum { MOTOR_SIDE_RIGHT = 0, MOTOR_SIDE_LEFT } motor_side_t;

typedef enum {
  MOTOR_RET_OK = 0,
  MOTOR_RET_BUSY,      /* 正在安装/开/关动作中                  */
  MOTOR_RET_NOT_INIT,  /* 尚未完成安装初始化(先 motor_init)     */
  MOTOR_RET_FAULT,
  MOTOR_RET_ERR,
} motor_ret_t;

/* 由 freertos.c 的 MX_FREERTOS_Init 创建 */
void MotorTask(void *argument);

/* ---- 应用接口 ---- */
motor_ret_t motor_init(void);                       /* 初始化电机(安装指令), 电机自校准 */
motor_ret_t motor_open(motor_speed_t speed);        /* 上翻开盖, 到位自动停 */
motor_ret_t motor_close(motor_speed_t speed);       /* 下翻关盖, 到位自动停 */
void motor_power_set(int on);                      /* 电机12V电源开关 */
int  motor_power_get(void);
motor_state_t motor_get_state(void);                /* 查询状态 */
const char   *motor_state_str(motor_state_t s);

#ifdef __cplusplus
}
#endif
#endif /* MOTOR_H */
