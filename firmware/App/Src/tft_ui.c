/* TFT 128x160 / ST7735 / STM32F103 / CMSIS-RTOS2
 * Uses existing, hardware-tested st7735.c/.h. No framebuffer.
 * Pages: MOI TRUONG, GOC NGHIENG, DINH VI GPS. Joystick LEFT/RIGHT or UP/DOWN.
 * Event flags are read only: E-STOP remains latched by the alarm subsystem.
 */
#include "tft_ui.h"
#include "st7735.h"
#include "app_ipc.h"
#include "cmsis_os2.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* 1 = tu chuyen trang moi 6 giay de thu du 3 trang khi chua co code Joystick.
 * Tu dung chuyen trang khi nhan duoc su kien Joystick hop le.
 * Doi thanh 0 khi tich hop chinh thuc. */
#define UI_AUTOPAGE_DEMO 1

#define UI_W      128U
#define UI_H      160U
#define UI_BG     0x0821U
#define UI_PANEL  0x10A2U
#define UI_BLUE   0x1B3FU
#define UI_WHITE  0xFFFFU
#define UI_DIM    0xAD55U
#define UI_CYAN   0x07FFU
#define UI_GREEN  0x07E0U
#define UI_YELLOW 0xFFE0U
#define UI_RED    0xF800U

/* Chu 5x7, moi byte bieu dien 7 diem theo chieu doc. */
typedef struct { char ch; uint8_t col[5]; } UiGlyph;
static const UiGlyph kFont[] = {
    {'A',{0x7E,0x11,0x11,0x11,0x7E}}, {'B',{0x7F,0x49,0x49,0x49,0x36}},
    {'C',{0x3E,0x41,0x41,0x41,0x22}}, {'D',{0x7F,0x41,0x41,0x22,0x1C}},
    {'E',{0x7F,0x49,0x49,0x49,0x41}}, {'F',{0x7F,0x09,0x09,0x09,0x01}},
    {'G',{0x3E,0x41,0x49,0x49,0x7A}}, {'H',{0x7F,0x08,0x08,0x08,0x7F}},
    {'I',{0x00,0x41,0x7F,0x41,0x00}}, {'J',{0x20,0x40,0x41,0x3F,0x01}},
    {'K',{0x7F,0x08,0x14,0x22,0x41}}, {'L',{0x7F,0x40,0x40,0x40,0x40}},
    {'M',{0x7F,0x02,0x0C,0x02,0x7F}}, {'N',{0x7F,0x04,0x08,0x10,0x7F}},
    {'O',{0x3E,0x41,0x41,0x41,0x3E}}, {'P',{0x7F,0x09,0x09,0x09,0x06}},
    {'Q',{0x3E,0x41,0x51,0x21,0x5E}}, {'R',{0x7F,0x09,0x19,0x29,0x46}},
    {'S',{0x46,0x49,0x49,0x49,0x31}}, {'T',{0x01,0x01,0x7F,0x01,0x01}},
    {'U',{0x3F,0x40,0x40,0x40,0x3F}}, {'V',{0x1F,0x20,0x40,0x20,0x1F}},
    {'W',{0x7F,0x20,0x18,0x20,0x7F}}, {'X',{0x63,0x14,0x08,0x14,0x63}},
    {'Y',{0x07,0x08,0x70,0x08,0x07}}, {'Z',{0x61,0x51,0x49,0x45,0x43}},
    {'0',{0x3E,0x51,0x49,0x45,0x3E}}, {'1',{0x00,0x42,0x7F,0x40,0x00}},
    {'2',{0x42,0x61,0x51,0x49,0x46}}, {'3',{0x22,0x41,0x49,0x49,0x36}},
    {'4',{0x18,0x14,0x12,0x7F,0x10}}, {'5',{0x27,0x45,0x45,0x45,0x39}},
    {'6',{0x3C,0x4A,0x49,0x49,0x30}}, {'7',{0x01,0x71,0x09,0x05,0x03}},
    {'8',{0x36,0x49,0x49,0x49,0x36}}, {'9',{0x06,0x49,0x49,0x29,0x1E}},
    {':',{0x00,0x36,0x36,0x00,0x00}}, {'.',{0x00,0x60,0x60,0x00,0x00}},
    {'-',{0x08,0x08,0x08,0x08,0x08}}, {'+',{0x08,0x08,0x3E,0x08,0x08}},
    {'/',{0x20,0x10,0x08,0x04,0x02}}, {'%',{0x63,0x13,0x08,0x64,0x63}},
    {'?',{0x02,0x01,0x51,0x09,0x06}}, {'_',{0x40,0x40,0x40,0x40,0x40}}
};

typedef struct {
    SensorData_t sensor;
    ImuData_t imu;
    GpsData_t gps;
    bool sensorValid;
    bool imuValid;
    bool gpsValid;
    bool sdKnown;
    bool sdReady;
    uint32_t revision;
} UiState;

static UiState s_state;  /* Zero-initialized, no invented sensor readings. */

static const uint8_t *ui_glyph(char ch)
{
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
    for (uint32_t i = 0; i < sizeof(kFont) / sizeof(kFont[0]); ++i) {
        if (kFont[i].ch == ch) return kFont[i].col;
    }
    return kFont[sizeof(kFont) / sizeof(kFont[0]) - 2].col; /* '?' */
}

/* Gom cac pixel lien tiep thanh mot hinh chu nhat de giam so lenh SPI. */
static void ui_text(uint16_t x, uint16_t y, const char *s,
                    uint16_t color, uint8_t scale)
{
    if (s == NULL || scale == 0U) return;
    for (; *s; ++s) {
        if (x + 5U * scale > UI_W) break;
        if (*s != ' ') {
            const uint8_t *g = ui_glyph(*s);
            for (uint8_t col = 0; col < 5U; ++col) {
                uint8_t row = 0U;
                while (row < 7U) {
                    if (((g[col] >> row) & 1U) == 0U) { ++row; continue; }
                    uint8_t first = row;
                    while (row < 7U && ((g[col] >> row) & 1U) != 0U) ++row;
                    (void)ST7735_FillRect((uint16_t)(x + col * scale),
                                          (uint16_t)(y + first * scale),
                                          scale, (uint16_t)(row - first) * scale,
                                          color);
                }
            }
        }
        x = (uint16_t)(x + 6U * scale);
    }
}

static void ui_fixed1(char *dst, size_t cap, float v)
{
    /* 999.9 is more than sufficient for temperature, attitude and speed. */
    if (!(v >= -999.9f && v <= 999.9f)) {
        (void)snprintf(dst, cap, "--");
        return;
    }
    int32_t tenths = (int32_t)(v >= 0.0f ? v * 10.0f + 0.5f : v * 10.0f - 0.5f);
    const char *sign = tenths < 0 ? "-" : "";
    if (tenths < 0) tenths = -tenths;
    (void)snprintf(dst, cap, "%s%ld.%01ld", sign,
                   (long)(tenths / 10), (long)(tenths % 10));
}

static void ui_coord(char *dst, size_t cap, double degrees)
{
    if (!(degrees >= -180.0 && degrees <= 180.0)) {
        (void)snprintf(dst, cap, "--"); return;
    }
    int32_t n = (int32_t)(degrees >= 0.0 ? degrees * 100000.0 + 0.5
                                         : degrees * 100000.0 - 0.5);
    char sign = n < 0 ? '-' : '+';
    if (n < 0) n = -n;
    (void)snprintf(dst, cap, "%c%ld.%05ld", sign,
                   (long)(n / 100000), (long)(n % 100000));
}

static bool ui_lock(void)
{
    return mtxStateHandle != NULL && osMutexAcquire(mtxStateHandle, 10U) == osOK;
}

void TFT_UI_UpdateSensor(const SensorData_t *data)
{
    if (data == NULL || !ui_lock()) return;
    s_state.sensor = *data;
    s_state.sensorValid = true;
    ++s_state.revision;
    (void)osMutexRelease(mtxStateHandle);
}

void TFT_UI_UpdateImu(const ImuData_t *data)
{
    if (data == NULL || !ui_lock()) return;
    s_state.imu = *data;
    s_state.imuValid = true;
    ++s_state.revision;
    (void)osMutexRelease(mtxStateHandle);
}

void TFT_UI_UpdateGps(const GpsData_t *data)
{
    if (data == NULL || !ui_lock()) return;
    s_state.gps = *data;
    s_state.gpsValid = true;
    ++s_state.revision;
    (void)osMutexRelease(mtxStateHandle);
}

void TFT_UI_SetSdReady(bool ready)
{
    if (!ui_lock()) return;
    s_state.sdKnown = true;
    s_state.sdReady = ready;
    ++s_state.revision;
    (void)osMutexRelease(mtxStateHandle);
}

static void ui_snapshot(UiState *out)
{
    if (ui_lock()) {
        *out = s_state;
        (void)osMutexRelease(mtxStateHandle);
    }
}

static void ui_field(uint16_t y, const char *key, const char *value,
                     uint16_t color)
{
    ui_text(7U, y, key, UI_DIM, 1U);
    ui_text(52U, y, value, color, 1U);
    (void)ST7735_FillRect(6U, y + 12U, 116U, 1U, UI_PANEL);
}

static void ui_alarm(uint32_t flags)
{
    uint16_t bg = UI_GREEN;
    const char *text = "HE THONG OK";
    if (flags & BIT_ESTOP) { bg = UI_RED; text = "DUNG KHAN CAP"; }
    else if (flags & BIT_GAS) { bg = UI_RED; text = "CANH BAO KHI"; }
    else if (flags & BIT_TILT) { bg = UI_YELLOW; text = "CANH BAO NGHIENG"; }
    (void)ST7735_FillRect(0U, 131U, UI_W, 16U, bg);
    ui_text(7U, 135U, text, UI_BG, 1U);
}

static void ui_frame(uint8_t page, uint32_t flags)
{
    static const char *titles[] = { "MOI TRUONG", "GOC NGHIENG", "DINH VI GPS" };
    static const char *counts[] = { "1/3", "2/3", "3/3" };
    (void)ST7735_FillScreen(UI_BG);
    (void)ST7735_FillRect(0U, 0U, UI_W, 22U, UI_BLUE);
    ui_text(5U, 7U, titles[page], UI_WHITE, 1U);
    ui_text(105U, 7U, counts[page], UI_WHITE, 1U);
    ui_alarm(flags);
    (void)ST7735_FillRect(0U, 150U, UI_W, 10U, UI_PANEL);
    ui_text(13U, 151U, "GAT CAN DOI TRANG", UI_DIM, 1U);
}

static void ui_env(const UiState *s)
{
    char buf[24];
    if (s->sensorValid) ui_fixed1(buf, sizeof(buf), s->sensor.temperature);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(30U, "NHIET:", buf, UI_CYAN);

    if (s->sensorValid) ui_fixed1(buf, sizeof(buf), s->sensor.humidity);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(50U, "DO AM:", buf, UI_CYAN);

    if (s->sensorValid) (void)snprintf(buf, sizeof(buf), "%u ADC", (unsigned)s->sensor.gas);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(70U, "KHI:", buf, UI_YELLOW);

    ui_field(90U, "THE SD:", !s->sdKnown ? "--" : (s->sdReady ? "SAN SANG" : "LOI"),
             s->sdReady ? UI_GREEN : UI_DIM);
    ui_field(110U, "GPS:", !s->gpsValid ? "--" : (s->gps.fix ? "FIX" : "CHUA FIX"),
             s->gpsValid && s->gps.fix ? UI_GREEN : UI_DIM);
}

static void ui_imu(const UiState *s)
{
    char buf[24];
    if (s->imuValid) ui_fixed1(buf, sizeof(buf), s->imu.pitch);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(32U, "DOC:", buf, UI_CYAN);

    if (s->imuValid) ui_fixed1(buf, sizeof(buf), s->imu.roll);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(56U, "NGANG:", buf, UI_CYAN);

    if (s->imuValid) ui_fixed1(buf, sizeof(buf), s->imu.gyroZ);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(80U, "XOAY Z:", buf, UI_YELLOW);

    ui_field(108U, "IMU:", s->imuValid ? "SAN SANG" : "--",
             s->imuValid ? UI_GREEN : UI_DIM);
}

static void ui_gps(const UiState *s)
{
    char buf[24];
    if (s->gpsValid && s->gps.fix) ui_coord(buf, sizeof(buf), s->gps.latitude);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(30U, "VI DO:", buf, UI_CYAN);

    if (s->gpsValid && s->gps.fix) ui_coord(buf, sizeof(buf), s->gps.longitude);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(52U, "KINH:", buf, UI_CYAN);

    if (s->gpsValid) (void)snprintf(buf, sizeof(buf), "%u", (unsigned)s->gps.satellites);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(74U, "TINH:", buf, UI_YELLOW);

    if (s->gpsValid && s->gps.fix) ui_fixed1(buf, sizeof(buf), s->gps.speed);
    else (void)snprintf(buf, sizeof(buf), "--");
    ui_field(96U, "TOC DO:", buf, UI_CYAN);

    ui_text(7U, 117U, s->gpsValid && s->gps.fix ? "DA DINH VI" : "DOI TIN HIEU",
            s->gpsValid && s->gps.fix ? UI_GREEN : UI_DIM, 1U);
}

void TFT_UI_Run(void)
{
    if (ST7735_Init() != HAL_OK) {
        for (;;) osDelay(1000U);
    }

    uint8_t page = 0U;
    uint8_t lastPage = 255U;
    uint32_t lastRevision = UINT32_MAX;
    uint32_t lastFlags = UINT32_MAX;
    uint32_t lastDrawTick = 0U;
    uint32_t lastAutoTick = osKernelGetTickCount();
    bool joyConnected = false;
    UiState view;
    (void)memset(&view, 0, sizeof(view));

    for (;;) {
        uint32_t joy = (uint32_t)JOY_NONE; /* qJoy is created with uint32_t items. */
        if (qJoyHandle != NULL && osMessageQueueGet(qJoyHandle, &joy, NULL, 0U) == osOK) {
            if (joy == (uint32_t)JOY_RIGHT || joy == (uint32_t)JOY_DOWN) {
                page = (uint8_t)((page + 1U) % 3U);
                joyConnected = true;
            } else if (joy == (uint32_t)JOY_LEFT || joy == (uint32_t)JOY_UP) {
                page = (uint8_t)((page + 2U) % 3U);
                joyConnected = true;
            }
        }

        uint32_t flags = 0U;
        if (evAlarmHandle != NULL) flags = osEventFlagsGet(evAlarmHandle) & ALARM_ALL_BITS;
        ui_snapshot(&view);

        /* Render at most ~1 Hz for frequent sensor updates; immediately on page/alarm change. */
        uint32_t now = osKernelGetTickCount();
        uint32_t oneSecond = osKernelGetTickFreq();
        if (oneSecond == 0U) oneSecond = 1000U;
#if UI_AUTOPAGE_DEMO
        if (!joyConnected && (uint32_t)(now - lastAutoTick) >= 6U * oneSecond) {
            page = (uint8_t)((page + 1U) % 3U);
            lastAutoTick = now;
        }
#else
        (void)lastAutoTick;
        (void)joyConnected;
#endif
        bool urgent = (page != lastPage) || (flags != lastFlags);
        bool changed = (view.revision != lastRevision);
        if (urgent || (changed && (uint32_t)(now - lastDrawTick) >= oneSecond)) {
            ui_frame(page, flags);
            if (page == 0U) ui_env(&view);
            else if (page == 1U) ui_imu(&view);
            else ui_gps(&view);
            lastPage = page;
            lastFlags = flags;
            lastRevision = view.revision;
            lastDrawTick = osKernelGetTickCount();
        }
        osDelay(50U);
    }
}
