#include "task_sd.h"
#include "cmsis_os.h"
#include "app_ipc.h"
#include "app_cfg.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

extern SystemState g_systemState;

/* FatFS objects */
static FATFS sd_fs;
static FIL sd_file;

/* Debug variables */
volatile FRESULT sd_mount_result = FR_NOT_READY;
volatile FRESULT sd_file_result = FR_NOT_READY;

volatile UINT sd_bytes_written = 0;
volatile UINT sd_bytes_read = 0;

volatile uint8_t sd_file_test_ok = 0;

/* Test data */
static const char test_data[] =
    "STM32 MicroSD Test OK\r\n";

static char read_buffer[64];
/* Logging objects */
static FIL log_file;
static char log_buffer[256];
static SystemState log_state;
static uint8_t log_opened = 0;

/* Logging debug variables */
volatile FRESULT sd_log_result = FR_NOT_READY;
volatile uint32_t sd_log_count = 0;
volatile uint32_t sd_log_errors = 0;
volatile uint32_t sd_sync_count = 0;

/* Mount SD card */
FRESULT SD_Storage_Init(void)
{
    sd_mount_result = f_mount(&sd_fs, "0:", 1);

    return sd_mount_result;
}

/* Test writing and reading file */
FRESULT SD_Storage_Test(void)
{
    FRESULT res;
    FRESULT close_res;
    UINT bytes_written = 0;
    UINT bytes_read = 0;

    sd_file_test_ok = 0;
    sd_bytes_written = 0;
    sd_bytes_read = 0;

    memset(read_buffer, 0, sizeof(read_buffer));

    /* Create test file */
    res = f_open(
        &sd_file,
        "0:SDTEST.TXT",
        FA_CREATE_ALWAYS | FA_WRITE
    );

    if (res != FR_OK)
        goto done;

    /* Write test data */
    res = f_write(
        &sd_file,
        test_data,
        sizeof(test_data) - 1,
        &bytes_written
    );

    sd_bytes_written = bytes_written;

    /* Close file */
    close_res = f_close(&sd_file);

    if (res == FR_OK)
        res = close_res;

    if (res != FR_OK)
        goto done;

    /* Check written length */
    if (bytes_written != sizeof(test_data) - 1)
    {
        res = FR_DISK_ERR;
        goto done;
    }

    /* Open file for reading */
    res = f_open(
        &sd_file,
        "0:SDTEST.TXT",
        FA_READ
    );

    if (res != FR_OK)
        goto done;

    /* Read data */
    res = f_read(
        &sd_file,
        read_buffer,
        sizeof(read_buffer) - 1,
        &bytes_read
    );

    sd_bytes_read = bytes_read;

    close_res = f_close(&sd_file);

    if (res == FR_OK)
        res = close_res;

    if (res != FR_OK)
        goto done;

    /* Compare data */
    if (bytes_read == sizeof(test_data) - 1 &&
        memcmp(test_data,
               read_buffer,
               bytes_read) == 0)
    {
        sd_file_test_ok = 1;
    }
    else
    {
        res = FR_INT_ERR;
    }

done:
    sd_file_result = res;

    return res;
}

/* SD task */
/* Update SD status in shared system state */
static void SD_SetReady(bool ready)
{
    if (osMutexAcquire(mtxStateHandle, osWaitForever) == osOK)
    {
        g_systemState.sdReady = ready;
        osMutexRelease(mtxStateHandle);
    }
}

/* Convert floating point to scaled integer */
static long SD_Scale(double value, double scale)
{
    double result = value * scale;

    if (!isfinite(result) ||
        result > 2147483647.0 ||
        result < -2147483648.0)
    {
        return 0;
    }

    return (long)result;
}

/* Open or create CSV log file */
FRESULT SD_Log_Open(void)
{
    FRESULT res;
    UINT written = 0;

    static const char header[] =
        "time_ms,latitude_e6,longitude_e6,"
        "speed_x100,altitude_x100,satellites,fix,"
        "pitch_x100,roll_x100,"
        "gyroX_x100,gyroY_x100,gyroZ_x100,"
        "gas_adc,temp_x100,humidity_x100,"
        "alarm_flags\r\n";

    if (log_opened)
        return FR_OK;

    res = f_open(
        &log_file,
        "0:LOG.CSV",
        FA_OPEN_APPEND | FA_WRITE
    );

    if (res != FR_OK)
        goto done;

    log_opened = 1;

    /* Write header only if file is empty */
    if (f_size(&log_file) == 0)
    {
        res = f_write(
            &log_file,
            header,
            sizeof(header) - 1,
            &written
        );

        if (res == FR_OK &&
            written != sizeof(header) - 1)
        {
            res = FR_DISK_ERR;
        }

        if (res == FR_OK)
            res = f_sync(&log_file);

        if (res != FR_OK)
        {
            f_close(&log_file);
            log_opened = 0;
        }
    }

done:
    sd_log_result = res;

    return res;
}

/* Write one system data record */
FRESULT SD_Log_Write(const SystemState *state)
{
    FRESULT res;
    UINT written = 0;
    int length;

    if (!log_opened)
        return FR_NOT_READY;

    if (state == NULL)
        return FR_INVALID_PARAMETER;

    length = snprintf(
        log_buffer,
        sizeof(log_buffer),

        "%lu,%ld,%ld,%ld,%ld,%u,%u,"
        "%ld,%ld,%ld,%ld,%ld,"
        "%u,%ld,%ld,%lu\r\n",

        (unsigned long)HAL_GetTick(),

        SD_Scale(state->gps.latitude, 1000000),
        SD_Scale(state->gps.longitude, 1000000),
        SD_Scale(state->gps.speed, 100),
        SD_Scale(state->gps.altitude, 100),

        (unsigned int)state->gps.satellites,
        (unsigned int)state->gps.fix,

        SD_Scale(state->imu.pitch, 100),
        SD_Scale(state->imu.roll, 100),

        SD_Scale(state->imu.gyroX, 100),
        SD_Scale(state->imu.gyroY, 100),
        SD_Scale(state->imu.gyroZ, 100),

        (unsigned int)state->sensor.gas,

        SD_Scale(state->sensor.temperature, 100),
        SD_Scale(state->sensor.humidity, 100),

        (unsigned long)state->alarmFlags
    );

    if (length <= 0 ||
        length >= (int)sizeof(log_buffer))
    {
        sd_log_result = FR_INT_ERR;
        sd_log_errors++;

        return FR_INT_ERR;
    }

    res = f_write(
        &log_file,
        log_buffer,
        (UINT)length,
        &written
    );

    if (res == FR_OK &&
        written != (UINT)length)
    {
        res = FR_DISK_ERR;
    }

    sd_log_result = res;

    if (res == FR_OK)
        sd_log_count++;
    else
        sd_log_errors++;

    return res;
}

/* Synchronize data to SD card */
FRESULT SD_Log_Sync(void)
{
    FRESULT res;

    if (!log_opened)
        return FR_NOT_READY;

    res = f_sync(&log_file);

    sd_log_result = res;

    if (res == FR_OK)
        sd_sync_count++;
    else
        sd_log_errors++;

    return res;
}

/* Close log and release storage */
static void SD_Log_Close(void)
{
    if (log_opened)
    {
        f_close(&log_file);
        log_opened = 0;
    }

    f_mount(NULL, "0:", 0);
    SD_SetReady(false);
}

/* SD logging task */
void Task_SD_Run(void *argument)
{
    uint32_t last_sync = HAL_GetTick();

    (void)argument;

    SD_SetReady(false);

    for (;;)
    {
        if (!log_opened)
        {
            if (SD_Storage_Init() != FR_OK)
            {
                osDelay(2000);
                continue;
            }

            if (SD_Log_Open() != FR_OK)
            {
                SD_Log_Close();
                osDelay(2000);
                continue;
            }

            SD_SetReady(true);
            last_sync = HAL_GetTick();
        }

        /* Copy shared data under mutex */
        if (osMutexAcquire(
            mtxStateHandle, osWaitForever) == osOK)
        {
            log_state = g_systemState;

            osMutexRelease(mtxStateHandle);

            /* Write outside the mutex */
            if (SD_Log_Write(&log_state) != FR_OK)
            {
                SD_Log_Close();
                osDelay(2000);
                continue;
            }
        }

        /* Synchronize every 5 seconds */
        if ((HAL_GetTick() - last_sync)
            >= LOG_SYNC_PERIOD_MS)
        {
            if (SD_Log_Sync() != FR_OK)
            {
                SD_Log_Close();
                osDelay(2000);
                continue;
            }

            last_sync = HAL_GetTick();
        }

        osDelay(LOG_TASK_PERIOD_MS);
    }
}