#ifndef TASK_IMU_H
#define TASK_IMU_H

#include <stdbool.h>
#include "app_types.h"

/* Gọi từ StartTaskIMU() trong main.c (giống cách Task_GUI gọi TFT_UI_Run).
 * Hàm chạy vô hạn, không bao giờ return. */
void TaskIMU_Run(void);

/* Task_Log (hoặc task khác) lấy dữ liệu IMU mới nhất. Trả về true nếu dữ liệu hợp lệ. */
bool TaskIMU_GetData(ImuData_t *out);

#endif /* TASK_IMU_H */
