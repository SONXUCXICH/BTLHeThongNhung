#ifndef APP_COMMON_H
#define APP_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include "FreeRTOS.h"
#include "queue.h"
#include "event_groups.h"

/* ---------- Độ ưu tiên task (cần configMAX_PRIORITIES >= 6) ---------- */
#define PRIO_LOG      (tskIDLE_PRIORITY + 1)
#define PRIO_SENSOR   (tskIDLE_PRIORITY + 2)
#define PRIO_GUI      (tskIDLE_PRIORITY + 2)
#define PRIO_GPS      (tskIDLE_PRIORITY + 3)
#define PRIO_IMU      (tskIDLE_PRIORITY + 4)
#define PRIO_ALARM    (tskIDLE_PRIORITY + 5)

/* ---------- Event Group: bit cảnh báo gửi cho Task_Alarm ---------- */
#define EVT_ESTOP     (1u << 0)   /* do ISR EXTI15 set */
#define EVT_TILT      (1u << 1)   /* Task_IMU set/clear */
#define EVT_GAS       (1u << 2)   /* Task_Sensor set/clear */

/* ---------- Dữ liệu IMU ---------- */
typedef struct {
    float    pitch_deg;
    float    roll_deg;
    bool     valid;       /* false nếu đọc MPU6050 lỗi */
    bool     tilt_alarm;
    uint32_t tick;
} ImuData_t;

/* ---------- Dữ liệu cảm biến ---------- */
typedef struct {
    uint16_t gas_raw;        /* ADC thô 0..4095 */
    uint16_t gas_filt;       /* ADC sau lọc IIR */
    uint16_t gas_mv;         /* điện áp AO trước chia áp (mV) */
    bool     gas_warming_up; /* MQ-2 đang làm nóng, chưa báo động */
    bool     gas_alarm;
    uint16_t joy_x;
    uint16_t joy_y;
    uint8_t  temp_c;         /* DHT11 */
    uint8_t  hum_pct;
    bool     dht_valid;
} SensorData_t;

/* ---------- Sự kiện Joystick (chuyển trang GUI) ---------- */
typedef enum { JOY_NONE = 0, JOY_LEFT, JOY_RIGHT, JOY_UP, JOY_DOWN } JoyEvent_t;

/* ---------- Handle dùng chung ---------- */
/* xAlarmEvents: tạo trong main.c TRƯỚC khi tạo task:
 *   xAlarmEvents = xEventGroupCreate();                                   */
extern EventGroupHandle_t xAlarmEvents;

/* Queue độ dài 1, ghi bằng xQueueOverwrite. Bên đọc dùng xQueuePeek(q,&d,0)
 * để luôn lấy giá trị mới nhất mà không làm rỗng queue (GUI và Log cùng đọc). */
extern QueueHandle_t xImuQueue;      /* ImuData_t    */
extern QueueHandle_t xSensorQueue;   /* SensorData_t */
extern QueueHandle_t xJoyQueue;      /* JoyEvent_t, độ dài 4, GUI xQueueReceive */

void TaskIMU_Create(void);
void TaskSensor_Create(void);

#endif /* APP_COMMON_H */
