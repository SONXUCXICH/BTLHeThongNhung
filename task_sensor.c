/* task_sensor.c - Task_Sensor
 * - ADC1 scan 3 kênh + DMA circular: PA0 = MQ-2 (AO), PA1 = Joystick X, PA2 = Joystick Y
 * - DHT11 một dây trên PB5 (polling có timeout, ~2 s/lần)
 * Chu kỳ 100 ms. Gas vượt ngưỡng -> set EVT_GAS cho Task_Alarm.
 */
#include "app_common.h"
#include "main.h"
#include "task.h"

extern ADC_HandleTypeDef hadc1;

/* ---------- Tham số ---------- */
#define SENSOR_PERIOD_MS   100
#define DHT_EVERY_N        20          /* 20 x 100 ms = 2 s */

#define GAS_WARMUP_MS      30000       /* MQ-2 cần làm nóng, chưa báo động trong 30 s đầu */
#define GAS_ON_RAW         2200        /* NGƯỠNG GIẢ ĐỊNH - phải chỉnh theo thực nghiệm */
#define GAS_OFF_RAW        1900        /* thấp hơn ON để có hysteresis */
#define GAS_CONFIRM        3           /* 3 chu kỳ liên tiếp = 300 ms */

/* Cầu chia áp AO (5 V -> <= 3.3 V): R_trên = 10k, R_dưới = 15k => hệ số 25/15 */
#define DIV_NUM            25
#define DIV_DEN            15

#define JOY_HI             3000        /* > HI: đẩy về một phía */
#define JOY_LO             1000        /* < LO: đẩy về phía kia, giữa = vùng chết */

/* ---------- ADC + DMA ---------- */
#define ADC_CH_NUM         3
#define IDX_GAS            0           /* rank 1 = IN0 = PA0 */
#define IDX_JOY_X          1           /* rank 2 = IN1 = PA1 */
#define IDX_JOY_Y          2           /* rank 3 = IN2 = PA2 */
static volatile uint16_t adc_buf[ADC_CH_NUM];

QueueHandle_t xSensorQueue;
QueueHandle_t xJoyQueue;

/* ---------- DHT11 trên PB5 ---------- */
#define DHT_PORT           GPIOB
#define DHT_PIN            GPIO_PIN_5
#define DHT_LOW()          (DHT_PORT->BSRR = ((uint32_t)DHT_PIN) << 16)
#define DHT_REL()          (DHT_PORT->BSRR = (uint32_t)DHT_PIN)       /* open-drain: thả cho pull-up kéo lên */
#define DHT_IN()           ((DHT_PORT->IDR & DHT_PIN) != 0)
#define CYC_PER_US         (SystemCoreClock / 1000000U)

static void dwt_init(void)              /* bộ đếm chu kỳ để đo micro giây */
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static void dht_gpio_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    DHT_REL();
    g.Pin   = DHT_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_OD;      /* cần pull-up ngoài 4.7 kΩ */
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(DHT_PORT, &g);
}

/* Chờ trong lúc chân còn ở mức `level`. Trả về thời gian (µs) chân giữ mức đó,
 * hoặc -1 nếu quá timeout. */
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

    /* Tín hiệu start: kéo thấp >= 18 ms (dùng vTaskDelay để không chặn CPU) */
    DHT_LOW();
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Phần đo thời gian ~4 ms: chặn ngắt có mức ưu tiên <= configMAX_SYSCALL_INTERRUPT_PRIORITY */
    taskENTER_CRITICAL();
    DHT_REL();
    if (dht_wait_while(1, 100) < 0) ok = false;          /* chờ DHT kéo xuống */
    if (ok && dht_wait_while(0, 120) < 0) ok = false;    /* DHT giữ thấp ~80 µs */
    if (ok && dht_wait_while(1, 120) < 0) ok = false;    /* DHT giữ cao ~80 µs */

    for (int i = 0; ok && i < 40; i++) {
        if (dht_wait_while(0, 80) < 0) { ok = false; break; }   /* ~50 µs mức thấp */
        int t = dht_wait_while(1, 100);                          /* mức cao: ~27 µs = bit 0, ~70 µs = bit 1 */
        if (t < 0) { ok = false; break; }
        d[i / 8] <<= 1;
        if (t > 40) d[i / 8] |= 1;
    }
    taskEXIT_CRITICAL();

    if (!ok) return false;
    if (((d[0] + d[1] + d[2] + d[3]) & 0xFF) != d[4]) return false;   /* checksum */

    *hum_pct = d[0];
    *temp_c  = d[2];
    return true;
}

static int8_t axis_state(uint16_t v)
{
    return (v > JOY_HI) ? 1 : ((v < JOY_LO) ? -1 : 0);
}

static void TaskSensor(void *arg)
{
    (void)arg;
    SensorData_t s = {0};

    dwt_init();
    dht_gpio_init();
    HAL_ADCEx_Calibration_Start(&hadc1);
    HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(void *)adc_buf, ADC_CH_NUM);

    TickType_t t0 = xTaskGetTickCount();
    uint32_t gas_f = 0;
    bool     gas_init = false, gas_alarm = false;
    uint8_t  gas_cnt = 0;
    int8_t   prev_x = 0, prev_y = 0;
    uint8_t  dht_fail = 0;
    uint8_t  dht_n = DHT_EVERY_N - 10;   /* DHT11 cần ~1 s sau khi cấp nguồn mới đọc lần đầu */

    TickType_t last = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(SENSOR_PERIOD_MS));

        /* DMA tự cập nhật mảng, đọc uint16 là nguyên tử nên không cần khóa */
        uint16_t gas = adc_buf[IDX_GAS];
        uint16_t jx  = adc_buf[IDX_JOY_X];
        uint16_t jy  = adc_buf[IDX_JOY_Y];

        /* ----- Gas: lọc IIR + hysteresis + xác nhận ----- */
        if (!gas_init) { gas_f = gas; gas_init = true; }
        else           { gas_f = (gas_f * 7 + gas) / 8; }

        bool warming = (xTaskGetTickCount() - t0) < pdMS_TO_TICKS(GAS_WARMUP_MS);
        if (!warming) {
            bool beyond = gas_alarm ? (gas_f > GAS_OFF_RAW) : (gas_f >= GAS_ON_RAW);
            if (beyond != gas_alarm) {
                if (++gas_cnt >= GAS_CONFIRM) {
                    gas_alarm = beyond;
                    gas_cnt = 0;
                    if (gas_alarm) xEventGroupSetBits(xAlarmEvents, EVT_GAS);
                    else           xEventGroupClearBits(xAlarmEvents, EVT_GAS);
                }
            } else {
                gas_cnt = 0;
            }
        }

        /* ----- Joystick: phát sự kiện theo cạnh (đẩy một lần = một sự kiện) ----- */
        int8_t sx = axis_state(jx), sy = axis_state(jy);
        JoyEvent_t ev = JOY_NONE;
        if      (sx != prev_x && sx ==  1) ev = JOY_RIGHT;
        else if (sx != prev_x && sx == -1) ev = JOY_LEFT;
        else if (sy != prev_y && sy ==  1) ev = JOY_UP;      /* đổi chiều nếu lắp ngược */
        else if (sy != prev_y && sy == -1) ev = JOY_DOWN;
        prev_x = sx;
        prev_y = sy;
        if (ev != JOY_NONE) xQueueSend(xJoyQueue, &ev, 0);   /* không chờ, đầy thì bỏ */

        /* ----- DHT11: mỗi ~2 s ----- */
        if (++dht_n >= DHT_EVERY_N) {
            dht_n = 0;
            uint8_t t, h;
            if (dht11_read(&t, &h)) {
                s.temp_c = t;
                s.hum_pct = h;
                s.dht_valid = true;
                dht_fail = 0;
            } else if (++dht_fail >= 3) {
                s.dht_valid = false;                         /* giữ giá trị cũ cho tới khi lỗi 3 lần liên tiếp */
                dht_fail = 3;
            }
        }

        s.gas_raw        = gas;
        s.gas_filt       = (uint16_t)gas_f;
        s.gas_mv         = (uint16_t)(gas_f * 3300UL * DIV_NUM / (4095UL * DIV_DEN));
        s.gas_warming_up = warming;
        s.gas_alarm      = gas_alarm;
        s.joy_x          = jx;
        s.joy_y          = jy;
        xQueueOverwrite(xSensorQueue, &s);
    }
}

void TaskSensor_Create(void)
{
    xSensorQueue = xQueueCreate(1, sizeof(SensorData_t));
    xJoyQueue    = xQueueCreate(4, sizeof(JoyEvent_t));
    configASSERT(xSensorQueue != NULL && xJoyQueue != NULL);
    xTaskCreate(TaskSensor, "Sensor", 256, NULL, PRIO_SENSOR, NULL);
}
