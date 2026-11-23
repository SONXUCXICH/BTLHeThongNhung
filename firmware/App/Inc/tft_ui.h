#ifndef TFT_UI_H
#define TFT_UI_H

#include "app_types.h"
#include <stdbool.h>

/* Goi tu StartTaskGUI() - ham chay vo han ben trong task FreeRTOS. */
void TFT_UI_Run(void);

/* Goi tu cac task sau khi doc duoc du lieu HOP LE. */
void TFT_UI_UpdateSensor(const SensorData_t *data);
void TFT_UI_UpdateImu(const ImuData_t *data);
void TFT_UI_UpdateGps(const GpsData_t *data);
void TFT_UI_SetSdReady(bool ready);

#endif /* TFT_UI_H */
