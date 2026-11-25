/* task_imu.c - Task_IMU
 * Gia tốc kế MMA845x (MMA8451/8452/8453) qua I2C1 -> Pitch/Roll.
 * Module không có gyro: ImuData_t.gyroX/Y/Z luôn = 0, góc tính từ gia tốc + lọc IIR.
 *  - Mỗi 10 ms (IMU_TASK_PERIOD_MS) đọc cảm biến, tính góc.
 *  - Vượt TILT_HIGH_DEG -> set BIT_TILT trên evAlarm; xuống dưới TILT_LOW_DEG -> clear.
 *  - Mỗi ~100 ms (GUI_TASK_PERIOD_MS) gọi TFT_UI_UpdateImu() với dữ liệu hợp lệ.
 */
#include "task_imu.h"
#include "app_cfg.h"
#include "app_ipc.h"
#include "tft_ui.h"
#include "main.h"
#include "FreeRTOS.h"
#include "task.h"
#include <math.h>

extern I2C_HandleTypeDef hi2c1;

/* ---------- MMA845x ---------- */
#define MMA_ADDR_SA0_LOW   0x1C         /* SA0 nối GND  */
#define MMA_ADDR_SA0_HIGH  0x1D         /* SA0 nối 3.3 V */
#define REG_OUT_X_MSB      0x01
#define REG_WHO_AM_I       0x0D
#define REG_XYZ_DATA_CFG   0x0E
#define REG_CTRL_REG1      0x2A
#define REG_CTRL_REG2      0x2B

#define WHO_MMA8451        0x1A
#define WHO_MMA8452        0x2A
#define WHO_MMA8453        0x3A

#define CTRL1_ACTIVE       0x01
#define CTRL1_LNOISE       0x04
#define CTRL1_DR_200HZ     0x10
#define CTRL2_RST          0x40
#define FS_2G              0x00

/* Dữ liệu 16 bit căn trái: ở thang ±2 g thì 1 g = 16384 count cho cả 3 đời chip */
#define ACC_LSB_PER_G      16384.0f
#define RAD2DEG            57.2957795f

/* ---------- Tham số riêng của task (ngưỡng nghiêng lấy từ app_cfg.h) ---------- */
#define IMU_LPF_K          0.15f        /* nhỏ = mượt hơn nhưng trễ hơn */
#define ACC_REJECT_G       0.35f        /* bỏ mẫu khi |a| lệch 1 g quá mức */
#define TILT_CONFIRM       20           /* 20 mẫu = 200 ms */
#define I2C_FAIL_LIMIT     10
#define IMU_GUI_EVERY_N    (GUI_TASK_PERIOD_MS / IMU_TASK_PERIOD_MS)

/* ms -> tick, đúng với mọi tốc độ tick */
#define TICKS(ms)  ((uint32_t)(((uint64_t)(ms) * osKernelGetTickFreq()) / 1000U))

typedef struct { float ax, ay, az; } Accel_t;   /* đơn vị g */

static uint16_t  mma_addr = (MMA_ADDR_SA0_LOW << 1);   /* địa chỉ 8 bit cho HAL */
static ImuData_t s_imu;
static bool      s_imu_valid;

/* ---------- Chia sẻ dữ liệu ---------- */
static void imu_store(const ImuData_t *d, bool valid)
{
    taskENTER_CRITICAL();
    if (d != NULL) s_imu = *d;
    s_imu_valid = valid;
    taskEXIT_CRITICAL();
}

bool TaskIMU_GetData(ImuData_t *out)
{
    if (out == NULL) return false;
    bool valid;
    taskENTER_CRITICAL();
    *out  = s_imu;
    valid = s_imu_valid;
    taskEXIT_CRITICAL();
    return valid;
}

/* ---------- Giao tiếp MMA845x ---------- */
static bool mma_write(uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(&hi2c1, mma_addr, reg, I2C_MEMADD_SIZE_8BIT,
                             &val, 1, 5) == HAL_OK;
}

static bool mma_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    return HAL_I2C_Mem_Read(&hi2c1, mma_addr, reg, I2C_MEMADD_SIZE_8BIT,
                            buf, len, 5) == HAL_OK;
}

/* Tự dò địa chỉ 0x1C / 0x1D bằng WHO_AM_I */
static bool mma_detect(void)
{
    static const uint8_t cand[2] = { MMA_ADDR_SA0_LOW, MMA_ADDR_SA0_HIGH };
    for (int i = 0; i < 2; i++) {
        uint8_t  who  = 0;
        uint16_t addr = (uint16_t)(cand[i] << 1);
        if (HAL_I2C_Mem_Read(&hi2c1, addr, REG_WHO_AM_I, I2C_MEMADD_SIZE_8BIT,
                             &who, 1, 5) == HAL_OK &&
            (who == WHO_MMA8451 || who == WHO_MMA8452 || who == WHO_MMA8453)) {
            mma_addr = addr;
            return true;
        }
    }
    return false;
}

static bool mma_init(void)
{
    if (!mma_detect()) return false;

    mma_write(REG_CTRL_REG2, CTRL2_RST);          /* soft reset, có thể NACK nên bỏ qua kết quả */
    osDelay(TICKS(20));
    for (int i = 0; i < 10; i++) {                /* chờ bit RST tự xóa */
        uint8_t r2;
        if (mma_read(REG_CTRL_REG2, &r2, 1) && !(r2 & CTRL2_RST)) break;
        osDelay(TICKS(5));
    }

    bool ok = mma_write(REG_CTRL_REG1, 0x00)                  /* standby để đổi cấu hình */
           && mma_write(REG_XYZ_DATA_CFG, FS_2G)              /* ±2 g */
           && mma_write(REG_CTRL_REG1,
                        CTRL1_DR_200HZ | CTRL1_LNOISE | CTRL1_ACTIVE);
    osDelay(TICKS(20));
    return ok;
}

static bool mma_read_accel(Accel_t *a)
{
    uint8_t b[6];
    if (!mma_read(REG_OUT_X_MSB, b, sizeof(b))) return false;
    int16_t x = (int16_t)((b[0] << 8) | b[1]);
    int16_t y = (int16_t)((b[2] << 8) | b[3]);
    int16_t z = (int16_t)((b[4] << 8) | b[5]);
    a->ax = x / ACC_LSB_PER_G;
    a->ay = y / ACC_LSB_PER_G;
    a->az = z / ACC_LSB_PER_G;
    return true;
}

/* ---------- Task ---------- */
void TaskIMU_Run(void)
{
    ImuData_t out = {0};                /* gyroX/Y/Z giữ 0: module không có gyro */
    Accel_t   f = {0, 0, 0};            /* vector gia tốc đã lọc */
    bool      first = true, tilt_active = false;
    uint8_t   tilt_cnt = 0, fail = 0, gui_n = 0;
    

    /* Chờ cảm biến sẵn sàng; chưa cắm thì dữ liệu giữ trạng thái không hợp lệ */
    while (!mma_init()) {
        imu_store(NULL, false);
        TFT_UI_SetImuValid(false);
        osDelay(TICKS(200));
    }

    uint32_t next = osKernelGetTickCount();

    for (;;) {
        next += TICKS(IMU_TASK_PERIOD_MS);
        osDelayUntil(next);

        Accel_t a;
        if (!mma_read_accel(&a)) {
            imu_store(NULL, false);
            TFT_UI_SetImuValid(false);
            if (++fail >= I2C_FAIL_LIMIT) {
                /* Thu khoi phuc I2C. Neu MMA chua san sang, tiep tuc thu. */
                (void)HAL_I2C_DeInit(&hi2c1);
                if (HAL_I2C_Init(&hi2c1) == HAL_OK) {
                    (void)mma_init();
                }
                fail = 0;
                first = true;
                next = osKernelGetTickCount();
            }
            continue;
        }
        fail = 0;

        /* Đang rung hoặc gia tốc mạnh: hướng trọng lực không đáng tin -> giữ góc cũ */
        float mag = sqrtf(a.ax * a.ax + a.ay * a.ay + a.az * a.az);
        if (fabsf(mag - 1.0f) > ACC_REJECT_G) continue;

        if (first) {
            f = a;
            first = false;
        } else {
            f.ax += IMU_LPF_K * (a.ax - f.ax);
            f.ay += IMU_LPF_K * (a.ay - f.ay);
            f.az += IMU_LPF_K * (a.az - f.az);
        }

        float roll = atan2f(f.ay, fabsf(f.az)) * RAD2DEG;
        float pitch = atan2f(-f.ax, sqrtf(f.ay * f.ay + f.az * f.az)) * RAD2DEG;
            
    
        /* Cảnh báo lật/đổ: hysteresis (HIGH/LOW trong app_cfg.h) + xác nhận nhiều mẫu */
        float thr    = tilt_active ? TILT_LOW_DEG : TILT_HIGH_DEG;
        bool  beyond = (fabsf(pitch) > thr) || (fabsf(roll) > thr);
        if (beyond != tilt_active) {
            if (++tilt_cnt >= TILT_CONFIRM) {
                tilt_active = beyond;
                tilt_cnt = 0;
                if (tilt_active) osEventFlagsSet(evAlarmHandle, BIT_TILT);
                else             osEventFlagsClear(evAlarmHandle, BIT_TILT);
            }
        } else {
            tilt_cnt = 0;
        }

        out.pitch = pitch;
        out.roll  = roll;
        imu_store(&out, true);

        /* Chỉ đẩy lên GUI mỗi ~100 ms, không phải mỗi 10 ms */
        if (++gui_n >= IMU_GUI_EVERY_N) {
            gui_n = 0;
            TFT_UI_UpdateImu(&out);
        }
    }
}
