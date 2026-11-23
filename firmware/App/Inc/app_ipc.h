#ifndef APP_IPC_H
#define APP_IPC_H

#include "cmsis_os2.h"
#include "app_types.h"

/* Mutex bao ve du lieu he thong */
extern osMutexId_t mtxStateHandle;

/* Binary Semaphore */
extern osSemaphoreId_t semGpsIdleHandle;
extern osSemaphoreId_t semTftDmaHandle;

/* Message Queue */
extern osMessageQueueId_t qJoyHandle;
extern osMessageQueueId_t qLogEventHandle;

/* Event Flags */
extern osEventFlagsId_t evAlarmHandle;

/* Cac bit canh bao */
#define BIT_ESTOP (1U << 0)
#define BIT_TILT  (1U << 1)
#define BIT_GAS   (1U << 2)

#define ALARM_ALL_BITS (BIT_ESTOP | BIT_TILT | BIT_GAS)

#endif /* APP_IPC_H */