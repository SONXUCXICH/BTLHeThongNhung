#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* Du lieu GPS */
typedef struct
{
    double latitude;
    double longitude;
    float speed;
    float altitude;
    uint8_t satellites;
    bool fix;
} GpsData_t;

/* Du lieu gia toc ke MMA845x: gyroX/Y/Z khong duoc su dung */
typedef struct
{
    float pitch;
    float roll;
    float gyroX;
    float gyroY;
    float gyroZ;
} ImuData_t;

/* Du lieu cam bien moi truong */
typedef struct
{
    uint16_t gas;
    float temperature;
    float humidity;
} SensorData_t;

/* Su kien Joystick */
typedef enum
{
    JOY_NONE = 0,
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT
} JoyEvent_t;

/* Su kien canh bao */
typedef enum
{
    LOG_NONE = 0,
    LOG_ESTOP,
    LOG_TILT,
    LOG_GAS
} LogEvent_t;

/* Trang thai toan he thong */
typedef struct
{
    GpsData_t gps;
    ImuData_t imu;
    SensorData_t sensor;

    bool sdReady;
    bool gpsReady;
    bool imuReady;

    uint32_t alarmFlags;
} SystemState;

#endif