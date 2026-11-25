/* task_sensor.c - Task_Sensor
 * - ADC1 quét 3 kênh + DMA circular: adc_buf[0] = PA0 (MQ-2 AO), [1] = PA1 (VRx), [2] = PA2 (VRy)
 * - DHT11 trên PB5 (DHT11_DATA), polling có timeout, mỗi DHT11_READ_PERIOD_MS
 * Vòng 20 ms: quét joystick -> gửi JoyEvent_t (app_types.h) vào qJoy.
 * Cứ SENSOR_TASK_PERIOD_MS (100 ms): lọc gas, báo động BIT_GAS trên evAlarm,
 * roi goi TFT_UI_UpdateSensorStatus().
 */
#include "task_sensor.h"
#include "app_cfg.h"
#include "app_ipc.h"
#include "tft_ui.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"

extern ADC_HandleTypeDef hadc1;           /* định nghĩa trong main.c */

/* ---------- Tham số riêng của task (ngưỡng gas lấy từ app_cfg.h) ---------- */
#define SENSOR_SCAN_MS     20             /* chu kỳ quét joystick */
#define SLOW_EVERY_N       (SENSOR_TASK_PERIOD_MS / SENSOR_SCAN_MS)    /* = 5 */
#define DHT_EVERY_N        (DHT11_READ_PERIOD_MS / SENSOR_TASK_PERIOD_MS)  /* = 20 */

#define JOY_HI             3000
#define JOY_LO             1000
#define JOY_DEBOUNCE       2              /* 2 lần quét liên tiếp (~40 ms) */

#define GAS_WARMUP_MS      30000          /* MQ-2 làm nóng, chưa báo động trong 30 s đầu */
#define GAS_CONFIRM        3

#define TICKS(ms)  ((uint32_t)(((uint64_t)(ms) * osKernelGetTickFreq()) / 1000U))

/* ---------- ADC + DMA ---------- */
#define IDX_GAS            0
#define IDX_JOY_X          1
#define IDX_JOY_Y          2
static volatile uint16_t adc_buf[ADC_CHANNEL_COUNT];

static SensorData_t s_sensor;
static bool         s_sensor_valid;

/* Dem loi DHT11 de debug bang GDB; khong anh huong den FreeRTOS. */
volatile uint32_t g_dht_ok_count = 0U;
volatile uint32_t g_dht_fail_count = 0U;
/* 0=OK, 1..3=phan hoi, 4..5=xung du lieu, 6=sai checksum. */
volatile uint32_t g_dht_last_error = 0U;

static void sensor_store(const SensorData_t *d)
{
    taskENTER_CRITICAL();
    s_sensor = *d;
    s_sensor_valid = true;
    taskEXIT_CRITICAL();
}

bool TaskSensor_GetData(SensorData_t *out)
{
    if (out == NULL) return false;
    bool valid;
    taskENTER_CRITICAL();
    *out  = s_sensor;
    valid = s_sensor_valid;
    taskEXIT_CRITICAL();
    return valid;
}

/* ---------- Joystick ---------- */
typedef struct { int8_t stable, cand; uint8_t cnt; } Axis_t;

/* true khi trạng thái trục (-1 / 0 / +1) vừa đổi và đã ổn định */
static bool axis_update(Axis_t *a, uint16_t v)
{
    int8_t raw = (v > JOY_HI) ? 1 : ((v < JOY_LO) ? -1 : 0);
    if (raw == a->stable) { a->cand = raw; a->cnt = 0; return false; }
    if (raw != a->cand)   { a->cand = raw; a->cnt = 1; return false; }
    if (++a->cnt >= JOY_DEBOUNCE) { a->stable = raw; a->cnt = 0; return true; }
    return false;
}

/* ---------- DHT11 ---------- */
#define DHT_PORT           DHT11_DATA_GPIO_Port    /* PB5, nhãn do CubeMX sinh */
#define DHT_PIN            DHT11_DATA_Pin
#define DHT_LOW()          (DHT_PORT->BSRR = ((uint32_t)DHT_PIN) << 16U)
#define DHT_REL()          (DHT_PORT->BSRR = (uint32_t)DHT_PIN)   /* open-drain: thả cho pull-up kéo lên */
#define DHT_IN()           ((DHT_PORT->IDR & DHT_PIN) != 0U)
#define CYC_PER_US         (SystemCoreClock / 1000000U)

static void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dht_gpio_init(void)
{
    /* main.c đặt PB5 là Input; đổi sang open-drain output để vừa kéo thấp vừa đọc được */
    GPIO_InitTypeDef g = {0};
    DHT_REL();
    g.Pin   = DHT_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;      /* cần pull-up ngoài 4.7 kΩ */
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT_PORT, &g);
}

/* Chờ trong lúc chân còn ở mức `level`; trả về thời gian (µs) giữ mức đó, -1 nếu quá timeout */
static int dht_wait_while(uint8_t level, uint32_t timeout_us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t limit = timeout_us * CYC_PER_US;
    while ((DHT_IN() ? 1 : 0) == level) {
        if ((DWT->CYCCNT - start) > limit) return -1;
    }
    return (int)((DWT->CYCCNT - start) / CYC_PER_US);
}

static bool dht11_read(uint8_t *temp_c, uint8_t *hum_pct)
{
    uint8_t d[5] = {0};
    bool ok = true;

    DHT_LOW();
    osDelay(TICKS(20));                  /* start: kéo thấp >= 18 ms, không chặn CPU */

    /* Tam dung chuyen task, nhung van cho ngat EXTI hoat dong.
     * Neu ISR chen vao lam sai timing, checksum/timeout se loai mau.
     * Day chi la giai phap cho demo; can timer capture neu yeu cau an toan cao. */
    taskENTER_CRITICAL();
    DHT_REL();
    if (dht_wait_while(1, 100) < 0) { ok = false; g_dht_last_error = 1U; }
    if (ok && dht_wait_while(0, 120) < 0) { ok = false; g_dht_last_error = 2U; }
    if (ok && dht_wait_while(1, 120) < 0) { ok = false; g_dht_last_error = 3U; }

    for (int i = 0; ok && i < 40; i++) {
        if (dht_wait_while(0, 80) < 0) { ok = false; g_dht_last_error = 4U; break; }
        int t = dht_wait_while(1, 100);  /* ~27 µs = bit 0, ~70 µs = bit 1 */
        if (t < 0) { ok = false; g_dht_last_error = 5U; break; }
        d[i / 8] <<= 1;
        if (t > 40) d[i / 8] |= 1;
    }
    taskEXIT_CRITICAL();

    if (!ok) return false;
    if (((d[0] + d[1] + d[2] + d[3]) & 0xFF) != d[4]) {
        g_dht_last_error = 6U;
        return false;
    }
    g_dht_last_error = 0U;

    *hum_pct = d[0];
    *temp_c  = d[2];
    return true;
}

/* ---------- Task ---------- */
void TaskSensor_Run(void)
{
    SensorData_t s = {0};      /* Gia tri cu chi hien khi da co mau DHT hop le. */
    bool dht_valid = false;
    uint8_t dht_fail_streak = 0U;

    dwt_init();
    dht_gpio_init();

    /* Neu ADC/DMA loi, KHONG dung ca he thong va khong doc buffer chua khoi tao.
     * Chi thu lai task cam bien; giao dien se hien -- cho cac gia tri. */
    while (true) {
        HAL_StatusTypeDef cal = HAL_ADCEx_Calibration_Start(&hadc1);
        HAL_StatusTypeDef dma = HAL_ERROR;
        if (cal == HAL_OK && hadc1.DMA_Handle != NULL) {
            dma = HAL_ADC_Start_DMA(&hadc1,
                                    (uint32_t *)(void *)adc_buf,
                                    ADC_CHANNEL_COUNT);
        }
        if (cal == HAL_OK && dma == HAL_OK) break;

        TFT_UI_UpdateSensorStatus(NULL, false, false);
        if (hadc1.DMA_Handle != NULL) {
            (void)HAL_ADC_Stop_DMA(&hadc1);
        }
        osDelay(1000U);
    }

    /* DMA 3 mau quet lien tuc: khong can ngat half/complete. */
    __HAL_DMA_DISABLE_IT(hadc1.DMA_Handle, DMA_IT_TC | DMA_IT_HT);

    Axis_t   ax = {0, 0, 0}, ay = {0, 0, 0};
    uint32_t t0 = osKernelGetTickCount();
    uint32_t gas_f = 0;
    bool     gas_init = false, gas_alarm = false;
    uint8_t  gas_cnt = 0, slow_n = 0;
    uint8_t  dht_n = DHT_EVERY_N - 10;   /* DHT11 cần ~1 s sau khi cấp nguồn mới đọc lần đầu */

    uint32_t next = osKernelGetTickCount();
    for (;;) {
        next += TICKS(SENSOR_SCAN_MS);
        osDelayUntil(next);

        /* ----- Joystick: quét mỗi 20 ms ----- */
        JoyEvent_t ev = JOY_NONE;
        if (axis_update(&ax, adc_buf[IDX_JOY_X])) {
            if      (ax.stable ==  1) ev = JOY_RIGHT;
            else if (ax.stable == -1) ev = JOY_LEFT;
        }
        if (axis_update(&ay, adc_buf[IDX_JOY_Y])) {
            if      (ay.stable ==  1) ev = JOY_UP;      /* đổi chiều nếu lắp ngược */
            else if (ay.stable == -1) ev = JOY_DOWN;
        }
        if (ev != JOY_NONE) {
            uint32_t msg = (uint32_t)ev;
            osMessageQueuePut(qJoyHandle, &msg, 0, 0);   /* không chờ, đầy thì bỏ */
        }

        /* ----- Phần chậm: mỗi 100 ms ----- */
        if (++slow_n < SLOW_EVERY_N) continue;
        slow_n = 0;

        uint16_t gas = adc_buf[IDX_GAS];
        if (!gas_init) { gas_f = gas; gas_init = true; }
        else           { gas_f = (gas_f * 7 + gas) / 8; }

        bool warming = (osKernelGetTickCount() - t0) < TICKS(GAS_WARMUP_MS);
        if (!warming) {
            /* Hysteresis theo GAS_HIGH_THRESHOLD / GAS_LOW_THRESHOLD trong app_cfg.h */
            bool beyond = gas_alarm ? (gas_f > GAS_LOW_THRESHOLD) : (gas_f >= GAS_HIGH_THRESHOLD);
            if (beyond != gas_alarm) {
                if (++gas_cnt >= GAS_CONFIRM) {
                    gas_alarm = beyond;
                    gas_cnt = 0;
                    if (gas_alarm) osEventFlagsSet(evAlarmHandle, BIT_GAS);
                    else           osEventFlagsClear(evAlarmHandle, BIT_GAS);
                }
            } else {
                gas_cnt = 0;
            }
        }

        /* DHT11 mỗi DHT11_READ_PERIOD_MS (đọc ~25 ms, osDelayUntil bù lại nhịp quét joystick) */
        if (++dht_n >= DHT_EVERY_N) {
            dht_n = 0;
            uint8_t t, h;
            if (dht11_read(&t, &h)) {
                ++g_dht_ok_count;
                dht_fail_streak = 0U;
                s.temperature = (float)t;
                s.humidity    = (float)h;
                dht_valid = true;
            } else {
                ++g_dht_fail_count;
                if (dht_fail_streak < 3U) ++dht_fail_streak;
                /* Mot lan loi do nhieu/xung doc khong lam mat ngay so do.
                 * Sau 3 lan loi lien tiep (~6 s) phai bao DHT khong hop le. */
                if (dht_fail_streak >= 3U) dht_valid = false;
            }
        }

        s.gas = (uint16_t)gas_f;
        sensor_store(&s);
        TFT_UI_UpdateSensorStatus(&s, true, dht_valid);
    }
}
