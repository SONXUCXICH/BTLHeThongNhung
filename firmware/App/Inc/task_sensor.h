#ifndef TASK_SENSOR_H
#define TASK_SENSOR_H

#include <stdbool.h>
#include "app_types.h"

/* Gọi từ StartTaskSensor() trong main.c. Hàm chạy vô hạn, không bao giờ return. */
void TaskSensor_Run(void);

/* Task_Log (hoặc task khác) lấy dữ liệu gas/nhiệt/ẩm mới nhất.
 * Trả về true nếu đã có ít nhất một lần đọc. */
bool TaskSensor_GetData(SensorData_t *out);

#endif /* TASK_SENSOR_H */
