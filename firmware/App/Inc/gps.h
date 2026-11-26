#ifndef GPS_H
#define GPS_H

#include <stdint.h>
#include <stdbool.h>
#include "app_types.h"

/* Khoi dong nhan du lieu GPS bang UART DMA */
bool GPS_Init(void);

/* Doc va xu ly du lieu moi trong DMA Circular */
void GPS_Process(void);

/* Lay du lieu GPS da phan tich */
bool GPS_GetData(GpsData_t *data);

#endif /* GPS_H */