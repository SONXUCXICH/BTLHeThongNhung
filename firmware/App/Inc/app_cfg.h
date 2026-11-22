#ifndef APP_CFG_H
#define APP_CFG_H

/* ==================================================
 * 1. SYSTEM CONFIGURATION
 * ================================================== */

#define APP_SYS_CLOCK_HZ       72000000UL

/* ==================================================
 * 2. GPS CONFIGURATION
 * ================================================== */

#define GPS_UART_BAUDRATE      9600U
#define GPS_DMA_BUFFER_SIZE    256U
#define GPS_NMEA_MAX_LENGTH    128U

/* ==================================================
 * 3. TASK PERIOD CONFIGURATION (milliseconds)
 * ================================================== */

#define IMU_TASK_PERIOD_MS     10U
#define SENSOR_TASK_PERIOD_MS  100U
#define GUI_TASK_PERIOD_MS     100U
#define LOG_TASK_PERIOD_MS     1000U
#define DHT11_READ_PERIOD_MS   2000U

/* ==================================================
 * 4. SENSOR CONFIGURATION
 * ================================================== */

#define ADC_CHANNEL_COUNT      3U
#define ADC_MAX_VALUE          4095U
#define ADC_VREF_MV            3300U

/* ==================================================
 * 5. ALARM CONFIGURATION
 * ================================================== */

/* Nguong canh bao ban dau, can hieu chuan */
#define TILT_HIGH_DEG          45.0f
#define TILT_LOW_DEG           40.0f

/* Nguong ADC MQ-2 tam thoi */
#define GAS_HIGH_THRESHOLD     2500U
#define GAS_LOW_THRESHOLD      2200U

#define ESTOP_DEBOUNCE_MS      50U
#define ESTOP_RESET_HOLD_MS    2000U

/* ==================================================
 * 6. TFT CONFIGURATION
 * ================================================== */

#define TFT_DMA_BUFFER_SIZE    2048U

/* ==================================================
 * 7. LOG CONFIGURATION
 * ================================================== */

#define LOG_SYNC_PERIOD_MS     5000U

#endif /* APP_CFG_H */