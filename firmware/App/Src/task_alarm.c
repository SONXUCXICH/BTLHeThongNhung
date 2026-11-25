#include "task_alarm.h"
#include "main.h"
#include "app_cfg.h"
#include "app_ipc.h"
#include "cmsis_os2.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

extern TIM_HandleTypeDef htim1;
extern SystemState g_systemState;

/* Existing EXTI callback in main.c sets this hint on falling edge.
 * The task debounces the PHYSICAL GPIO level, so a HIGH transition is
 * handled even though CubeMX currently enables falling-edge EXTI only. */
extern volatile uint8_t estop_pending;

volatile uint32_t alarm_debug_flags = 0U;
volatile uint32_t alarm_debug_mode = 0U;
volatile uint32_t alarm_debug_buzzer_on = 0U;
volatile uint32_t alarm_debug_log_dropped = 0U;
volatile uint32_t alarm_debug_pwm_ok = 0U;

#define ALARM_POLL_MS       10U
#define TILT_ON_MS         100U
#define TILT_OFF_MS        100U
#define GAS_ON_MS          500U
#define GAS_OFF_MS         500U

/* TIM1 runs at 72 MHz / (PSC+1) = 1 MHz with CubeMX PSC = 71. */
#define TIM1_COUNTER_HZ     1000000UL
#define ESTOP_TONE_HZ       2500UL
#define TILT_TONE_HZ        2000UL
#define GAS_TONE_HZ         1500UL

static uint32_t ticks_from_ms(uint32_t milliseconds)
{
    uint32_t hz = osKernelGetTickFreq();
    uint32_t ticks = (uint32_t)((((uint64_t)milliseconds * hz) + 999ULL) / 1000ULL);
    return (ticks == 0U) ? 1U : ticks;
}

static void buzzer_set_tone(uint32_t hz)
{
    /* Keep PSC=71 from the MX_TIM1_Init() configuration. */
    uint32_t period = TIM1_COUNTER_HZ / hz;
    if (period < 2U) period = 2U;
    __HAL_TIM_SET_AUTORELOAD(&htim1, period - 1U);
    __HAL_TIM_SET_COUNTER(&htim1, 0U);
}

static void buzzer_set_on(bool enabled)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(&htim1) + 1U;
#if ALARM_ACTIVE_BUZZER
    uint32_t ccr = enabled ? period : 0U;
#else
    uint32_t ccr = enabled ? (period / 2U) : 0U;
#endif
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, ccr);
    alarm_debug_buzzer_on = enabled ? 1U : 0U;
}

static uint32_t select_mode(uint32_t flags)
{
    if ((flags & BIT_ESTOP) != 0U) return 3U;
    if ((flags & BIT_TILT)  != 0U) return 2U;
    if ((flags & BIT_GAS)   != 0U) return 1U;
    return 0U;
}

static void log_onset(uint32_t flags, uint32_t previous_flags)
{
    uint32_t newly_active = flags & ~previous_flags;
    if (qLogEventHandle == NULL) return;

    /* qLogEvent is declared with sizeof(uint32_t) in main.c. */
    const uint32_t event_bits[3] = { BIT_ESTOP, BIT_TILT, BIT_GAS };
    const uint32_t log_codes[3] = { LOG_ESTOP, LOG_TILT, LOG_GAS };
    for (uint32_t i = 0U; i < 3U; ++i) {
        if ((newly_active & event_bits[i]) != 0U) {
            uint32_t msg = log_codes[i];
            if (osMessageQueuePut(qLogEventHandle, &msg, 0U, 0U) != osOK) {
                ++alarm_debug_log_dropped;
            }
        }
    }
}

/* Update the shared SystemState for other tasks, avoiding a long block
 * on high-priority Task_Alarm. GUI already reads evAlarmHandle itself. */
static void publish_state(uint32_t flags, uint32_t *last_published)
{
    if (*last_published == flags) return;
    if (mtxStateHandle != NULL && osMutexAcquire(mtxStateHandle, 0U) == osOK) {
        g_systemState.alarmFlags = flags;
        (void)osMutexRelease(mtxStateHandle);
        *last_published = flags;
    }
}

void TaskAlarm_Run(void)
{
    if (evAlarmHandle == NULL) {
        for (;;) osDelay(1000U);
    }

    /* Zero duty before enabling timer: prevents an unintended startup beep. */
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0U);
    buzzer_set_tone(TILT_TONE_HZ);
    alarm_debug_pwm_ok = (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK) ? 1U : 0U;
    if (alarm_debug_pwm_ok == 0U) {
        /* No false claim of buzzer operation; keep alarm flags available. */
        buzzer_set_on(false);
    }

    const uint32_t debounce_ticks = ticks_from_ms(ESTOP_DEBOUNCE_MS);
#if ALARM_REQUIRE_2S_RELEASE
    const uint32_t reset_hold_ticks = ticks_from_ms(ESTOP_RESET_HOLD_MS);
#endif
    const uint32_t wait_ticks = ticks_from_ms(ALARM_POLL_MS);
    uint32_t now = osKernelGetTickCount();
    GPIO_PinState raw_level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);
    GPIO_PinState stable_level = raw_level;
    uint32_t raw_change_tick = now;
    uint32_t phase_start_tick = now;
    uint32_t last_flags = 0U;
    uint32_t last_published = UINT32_MAX;
    uint32_t last_mode = UINT32_MAX;
    bool last_on = false;

    /* The physical latching switch defines E-Stop state. */
    if (stable_level == GPIO_PIN_RESET) {
        (void)osEventFlagsSet(evAlarmHandle, BIT_ESTOP);
    } else {
        (void)osEventFlagsClear(evAlarmHandle, BIT_ESTOP);
    }

    for (;;) {
        now = osKernelGetTickCount();
        GPIO_PinState current_level = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_15);
        if (current_level != raw_level) {
            raw_level = current_level;
            raw_change_tick = now;
        }
        if (current_level != stable_level &&
            (uint32_t)(now - raw_change_tick) >= debounce_ticks) {
#if ALARM_REQUIRE_2S_RELEASE
            if (current_level == GPIO_PIN_SET &&
                (uint32_t)(now - raw_change_tick) < reset_hold_ticks) {
                /* Hold HIGH long enough before releasing the E-Stop latch. */
            } else
#endif
            {
                stable_level = current_level;
                if (stable_level == GPIO_PIN_RESET) {
                    (void)osEventFlagsSet(evAlarmHandle, BIT_ESTOP);
                } else {
                    (void)osEventFlagsClear(evAlarmHandle, BIT_ESTOP);
                }
            }
        }

        /* ISR only signals a possible falling edge; state is confirmed above. */
        estop_pending = 0U;

        uint32_t flags = osEventFlagsGet(evAlarmHandle) & ALARM_ALL_BITS;
        alarm_debug_flags = flags;
        publish_state(flags, &last_published);
        log_onset(flags, last_flags);
        last_flags = flags;

        uint32_t mode = select_mode(flags);
        alarm_debug_mode = mode;
        bool mode_changed = (mode != last_mode);
        if (mode_changed) {
            phase_start_tick = now;
            last_mode = mode;
            if      (mode == 3U) buzzer_set_tone(ESTOP_TONE_HZ);
            else if (mode == 2U) buzzer_set_tone(TILT_TONE_HZ);
            else if (mode == 1U) buzzer_set_tone(GAS_TONE_HZ);
        }

        bool on = false;
        if (mode == 3U) {
            on = true;   /* E-Stop: continuous */
        } else if (mode == 2U) {
            uint32_t period = ticks_from_ms(TILT_ON_MS + TILT_OFF_MS);
            on = ((now - phase_start_tick) % period) < ticks_from_ms(TILT_ON_MS);
        } else if (mode == 1U) {
            uint32_t period = ticks_from_ms(GAS_ON_MS + GAS_OFF_MS);
            on = ((now - phase_start_tick) % period) < ticks_from_ms(GAS_ON_MS);
        }
        if (alarm_debug_pwm_ok == 0U) on = false;
        if (on != last_on || mode_changed) {
            buzzer_set_on(on);
            last_on = on;
        }

        /* Read state every 10 ms, but sleep instead of busy-waiting. When
         * safe, a new event can wake the task earlier (NoClear is required). */
        if (mode == 0U) {
            (void)osEventFlagsWait(evAlarmHandle, ALARM_ALL_BITS,
                                   osFlagsWaitAny | osFlagsNoClear, wait_ticks);
        } else {
            osDelay(wait_ticks);
        }
    }
}

/* Optional CMSIS-RTOS2 factory: used ONLY by a standalone test harness.
 * The current project already calls osThreadNew(StartTaskAlarm, ...) in main.c;
 * calling this in addition would create an unintended second Alarm thread. */
static void TaskAlarm_Entry(void *argument)
{
    (void)argument;
    TaskAlarm_Run();
}

osThreadId_t TaskAlarm_Create(void)
{
    const osThreadAttr_t attrs = {
        .name = "Task_Alarm",
        .priority = osPriorityHigh,
        .stack_size = 256U * 4U,
    };
    return osThreadNew(TaskAlarm_Entry, NULL, &attrs);
}
