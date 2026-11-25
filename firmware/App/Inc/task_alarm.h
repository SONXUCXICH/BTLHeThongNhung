#ifndef TASK_ALARM_H
#define TASK_ALARM_H

#include <stdint.h>
#include "cmsis_os2.h"

/*
 * Task_Alarm on STM32F103C8T6:
 * - PA8: TIM1_CH1 PWM for passive buzzer
 * - PA15: E-Stop latching NO switch, LOW = ON, HIGH = OFF
 * - Priority: E-Stop > Tilt > Gas
 * - evAlarmHandle is a persistent state bitmask (do NOT consume/auto-clear).
 */

/* 0: passive buzzer driven by PWM, 1: active-high buzzer, held HIGH when on. */
#ifndef ALARM_ACTIVE_BUZZER
#define ALARM_ACTIVE_BUZZER 0
#endif

/* Set to 1 only if the E-Stop release is to be confirmed by 2 seconds HIGH.
 * Default 0 follows the latching button: second push releases to HIGH. */
#ifndef ALARM_REQUIRE_2S_RELEASE
#define ALARM_REQUIRE_2S_RELEASE 0
#endif

/* Entry point for an existing CMSIS-RTOS2 Task_Alarm created elsewhere. */
void TaskAlarm_Run(void);

/* Optional factory for a standalone/mock test harness ONLY.
 * Do not call this if main.c already creates Task_Alarm. */
osThreadId_t TaskAlarm_Create(void);

/* GDB diagnostics (0 none; 1 gas; 2 tilt; 3 estop). */
extern volatile uint32_t alarm_debug_flags;
extern volatile uint32_t alarm_debug_mode;
extern volatile uint32_t alarm_debug_buzzer_on;
extern volatile uint32_t alarm_debug_log_dropped;
extern volatile uint32_t alarm_debug_pwm_ok;

#endif /* TASK_ALARM_H */
