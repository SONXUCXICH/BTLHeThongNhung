/* task_imu.c - Task_IMU
 * MPU6050 (I2C1, 400 kHz) -> Complementary Filter -> Pitch/Roll
 * Chu kỳ 10 ms. Vượt ngưỡng nghiêng -> set EVT_TILT cho Task_Alarm.
 */
#include "app_common.h"
#include "main.h"
#include "task.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;
void MX_I2C1_Init(void);            /* CubeMX sinh ra, dùng để hồi phục bus */

/* ---------- MPU6050 ---------- */
#define MPU_ADDR          (0x68 << 1)   /* AD0 = GND. Nếu AD0 = 3V3 thì 0x69 */
#define REG_SMPLRT_DIV    0x19
#define REG_CONFIG        0x1A
#define REG_GYRO_CFG      0x1B
#define REG_ACCEL_CFG     0x1C
#define REG_ACCEL_XOUT_H  0x3B
#define REG_PWR_MGMT_1    0x6B
#define REG_WHO_AM_I      0x75

#define ACC_LSB_PER_G     16384.0f      /* ±2 g   */
#define GYRO_LSB_PER_DPS  131.0f        /* ±250 °/s */
#define RAD2DEG           57.2957795f

/* ---------- Tham số thuật toán ---------- */
#define IMU_PERIOD_MS     10
#define CF_ALPHA          0.98f         /* tin gyro 98%, accel 2% */
#define TILT_ON_DEG       45.0f         /* vào cảnh báo khi vượt */
#define TILT_OFF_DEG      35.0f         /* ra cảnh báo khi xuống dưới (hysteresis) */
#define TILT_CONFIRM      10            /* 10 mẫu liên tiếp = 100 ms (chống nhiễu) */
#define I2C_FAIL_LIMIT    10            /* lỗi liên tiếp -> reset I2C */

QueueHandle_t xImuQueue;

typedef struct { int16_t ax, ay, az, gx, gy, gz; } MpuRaw_t;

static bool mpu_write(uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(&hi2c1, MPU_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                             &val, 1, 5) == HAL_OK;
}

static bool mpu_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1, MPU_ADDR, reg, I2C_MEMADD_SIZE_8BIT,
                            buf, len, 5) == HAL_OK;
}

static bool mpu_init(void)
{
    uint8_t who;
    if (!mpu_read(REG_WHO_AM_I, &who, 1)) return false;   /* kiểm tra chip có trả lời */
    /* Không so sánh who == 0x68 cứng vì module clone có thể trả giá trị khác */

    if (!mpu_write(REG_PWR_MGMT_1, 0x80)) return false;    /* reset */
    vTaskDelay(pdMS_TO_TICKS(100));

    bool ok = mpu_write(REG_PWR_MGMT_1, 0x01)   /* thoát sleep, clock = PLL gyro X */
           && mpu_write(REG_SMPLRT_DIV, 0x07)   /* 1 kHz / 8 = 125 Hz */
           && mpu_write(REG_CONFIG,     0x03)   /* DLPF ~44 Hz */
           && mpu_write(REG_GYRO_CFG,   0x00)   /* ±250 °/s */
           && mpu_write(REG_ACCEL_CFG,  0x00);  /* ±2 g */
    vTaskDelay(pdMS_TO_TICKS(50));
    return ok;
}

/* Đọc 14 byte một lần: Accel(6) + Temp(2) + Gyro(6) */
static bool mpu_read_raw(MpuRaw_t *r)
{
    uint8_t b[14];
    if (!mpu_read(REG_ACCEL_XOUT_H, b, sizeof(b))) return false;
    r->ax = (int16_t)((b[0]  << 8) | b[1]);
    r->ay = (int16_t)((b[2]  << 8) | b[3]);
    r->az = (int16_t)((b[4]  << 8) | b[5]);
    /* b[6], b[7] = nhiệt độ chip, bỏ qua */
    r->gx = (int16_t)((b[8]  << 8) | b[9]);
    r->gy = (int16_t)((b[10] << 8) | b[11]);
    r->gz = (int16_t)((b[12] << 8) | b[13]);
    return true;
}

/* Lấy độ lệch (bias) gyro lúc khởi động - mạch phải nằm yên ~1 s */
static void gyro_calibrate(float *bx, float *by)
{
    int32_t sx = 0, sy = 0;
    int n = 0;
    for (int i = 0; i < 200; i++) {
        MpuRaw_t r;
        if (mpu_read_raw(&r)) { sx += r.gx; sy += r.gy; n++; }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    *bx = n ? (float)sx / n : 0.0f;
    *by = n ? (float)sy / n : 0.0f;
}

static void TaskIMU(void *arg)
{
    (void)arg;
    ImuData_t out = {0};
    float bias_x = 0, bias_y = 0;
    float pitch = 0, roll = 0;
    bool  first = true, tilt_active = false;
    uint8_t tilt_cnt = 0, fail = 0;

    /* Chờ MPU6050 sẵn sàng (nếu chưa cắm, GUI sẽ thấy valid = false) */
    while (!mpu_init()) {
        out.valid = false;
        xQueueOverwrite(xImuQueue, &out);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    gyro_calibrate(&bias_x, &bias_y);

    TickType_t last = xTaskGetTickCount();
    TickType_t prev = last;

    for (;;) {
        vTaskDelayUntil(&last, pdMS_TO_TICKS(IMU_PERIOD_MS));
        float dt = (float)(last - prev) * portTICK_PERIOD_MS / 1000.0f;
        prev = last;

        MpuRaw_t r;
        if (!mpu_read_raw(&r)) {
            out.valid = false;
            xQueueOverwrite(xImuQueue, &out);
            if (++fail >= I2C_FAIL_LIMIT) {            /* bus I2C bị treo -> reset */
                HAL_I2C_DeInit(&hi2c1);
                MX_I2C1_Init();
                mpu_init();
                fail = 0;
                first = true;
            }
            continue;
        }
        fail = 0;

        /* Góc từ accelerometer (độ) */
        float ax = r.ax / ACC_LSB_PER_G;
        float ay = r.ay / ACC_LSB_PER_G;
        float az = r.az / ACC_LSB_PER_G;
        float acc_roll  = atan2f(ay, az) * RAD2DEG;
        float acc_pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * RAD2DEG;

        /* Tốc độ góc từ gyro (°/s) đã trừ bias */
        float gx = ((float)r.gx - bias_x) / GYRO_LSB_PER_DPS;
        float gy = ((float)r.gy - bias_y) / GYRO_LSB_PER_DPS;

        if (first) {                    /* khởi tạo góc từ accel */
            roll = acc_roll;
            pitch = acc_pitch;
            first = false;
        } else {
            roll  = CF_ALPHA * (roll  + gx * dt) + (1.0f - CF_ALPHA) * acc_roll;
            pitch = CF_ALPHA * (pitch + gy * dt) + (1.0f - CF_ALPHA) * acc_pitch;
        }

        /* Cảnh báo lật/đổ: hysteresis + xác nhận nhiều mẫu */
        float thr = tilt_active ? TILT_OFF_DEG : TILT_ON_DEG;
        bool beyond = (fabsf(pitch) > thr) || (fabsf(roll) > thr);
        if (beyond != tilt_active) {
            if (++tilt_cnt >= TILT_CONFIRM) {
                tilt_active = beyond;
                tilt_cnt = 0;
                if (tilt_active) xEventGroupSetBits(xAlarmEvents, EVT_TILT);
                else             xEventGroupClearBits(xAlarmEvents, EVT_TILT);
            }
        } else {
            tilt_cnt = 0;
        }

        out.pitch_deg  = pitch;
        out.roll_deg   = roll;
        out.valid      = true;
        out.tilt_alarm = tilt_active;
        out.tick       = last;
        xQueueOverwrite(xImuQueue, &out);
    }
}

void TaskIMU_Create(void)
{
    xImuQueue = xQueueCreate(1, sizeof(ImuData_t));
    configASSERT(xImuQueue != NULL);
    xTaskCreate(TaskIMU, "IMU", 256, NULL, PRIO_IMU, NULL);   /* stack 256 word = 1 KB */
}
