#ifndef TASK_SD_H
#define TASK_SD_H

#include "ff.h"
#include "app_types.h"

/* Debug variables */
extern volatile FRESULT sd_mount_result;
extern volatile FRESULT sd_file_result;
extern volatile UINT sd_bytes_written;
extern volatile UINT sd_bytes_read;
extern volatile uint8_t sd_file_test_ok;

/* Logging debug variables */
extern volatile FRESULT sd_log_result;
extern volatile uint32_t sd_log_count;
extern volatile uint32_t sd_log_errors;
extern volatile uint32_t sd_sync_count;

/* Public functions */
FRESULT SD_Storage_Init(void);

FRESULT SD_Storage_Test(void);

FRESULT SD_Log_Open(void);

FRESULT SD_Log_Write(const SystemState *state);

FRESULT SD_Log_Sync(void);

void Task_SD_Run(void *argument);

#endif