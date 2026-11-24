#include "gps.h"
#include "main.h"
#include "app_cfg.h"

#include <string.h>

/* UART1 duoc khoi tao boi STM32CubeMX */
extern UART_HandleTypeDef huart1;

/* Bo dem nhan UART bang DMA Circular */
static uint8_t gps_dma_buffer[GPS_DMA_BUFFER_SIZE];

/* Vi tri da doc trong bo dem DMA */
static uint16_t gps_old_pos = 0;

/* Bo dem luu mot cau NMEA */
static char gps_nmea_buffer[GPS_NMEA_MAX_LENGTH];

/* Vi tri hien tai trong cau NMEA */
static uint16_t gps_nmea_index = 0;

/* Du lieu GPS da duoc phan tich */
static GpsData_t gps_data;

/* Da nhan duoc cau RMC hop le ve cu phap hay chua */
static bool gps_has_rmc = false;

/* Theo doi DMA va thoi diem nhan cau RMC moi nhat */
#define GPS_SIGNAL_TIMEOUT_MS 3000U
#define GPS_RETRY_INTERVAL_MS 1000U
static bool gps_dma_running = false;
static uint32_t gps_last_rmc_tick = 0;
static uint32_t gps_last_retry_tick = 0;


/* =========================================================
   1. CHUYEN KY TU HEX SANG GIA TRI
   ========================================================= */

static int GPS_HexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';

    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;

    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;

    return -1;
}


/* =========================================================
   2. KIEM TRA CHECKSUM NMEA
   ========================================================= */

static bool GPS_Checksum(const char *sentence)
{
    if (sentence == NULL || sentence[0] != '$')
        return false;

    uint8_t checksum = 0;
    uint16_t i = 1;

    while (sentence[i] != '\0' &&
           sentence[i] != '*')
    {
        checksum ^= (uint8_t)sentence[i];
        i++;
    }

    if (sentence[i] != '*')
        return false;

    int high = GPS_HexValue(sentence[i + 1]);

    if (high < 0)
        return false;

    int low = GPS_HexValue(sentence[i + 2]);

    if (low < 0)
        return false;

    if (sentence[i + 3] != '\0')
        return false;

    uint8_t received =
        (uint8_t)((high << 4) | low);

    return checksum == received;
}


/* =========================================================
   3. CHUYEN CHUOI SANG SO THUC
   ========================================================= */

static bool GPS_ParseDecimal(const char *str,
                             double *result)
{
    if (str == NULL || result == NULL ||
        str[0] == '\0')
    {
        return false;
    }

    double value = 0.0;
    double factor = 0.1;

    bool negative = false;
    bool decimal_point = false;
    bool has_digit = false;

    uint16_t i = 0;

    if (str[i] == '-')
    {
        negative = true;
        i++;
    }
    else if (str[i] == '+')
    {
        i++;
    }

    while (str[i] != '\0')
    {
        char c = str[i];

        if (c == '.')
        {
            if (decimal_point)
                return false;

            decimal_point = true;
        }
        else if (c >= '0' && c <= '9')
        {
            has_digit = true;

            if (!decimal_point)
            {
                value = value * 10.0 +
                        (double)(c - '0');
            }
            else
            {
                value += (double)(c - '0') *
                         factor;

                factor *= 0.1;
            }
        }
        else
        {
            return false;
        }

        i++;
    }

    if (!has_digit)
        return false;

    if (negative)
        value = -value;

    *result = value;

    return true;
}


/* =========================================================
   4. CHUYEN CHUOI SANG SO NGUYEN
   ========================================================= */

static bool GPS_ParseUnsigned(const char *str,
                              uint32_t *result)
{
    if (str == NULL || result == NULL ||
        str[0] == '\0')
    {
        return false;
    }

    uint32_t value = 0;

    while (*str != '\0')
    {
        if (*str < '0' || *str > '9')
            return false;

        uint32_t digit = (uint32_t)(*str - '0');

        if (value > (UINT32_MAX - digit) / 10U)
            return false;

        value = value * 10U + digit;
        str++;
    }

    *result = value;

    return true;
}


/* =========================================================
   5. CHUYEN TOA DO NMEA SANG DO THAP PHAN
   ========================================================= */

static bool GPS_ConvertCoordinate(const char *raw,
                                  char direction,
                                  bool is_latitude,
                                  double *result)
{
    double value;

    if (!GPS_ParseDecimal(raw, &value))
        return false;

    if (value < 0.0)
        return false;

    uint32_t degrees = (uint32_t)(value / 100.0);

    double minutes = value -
                     (double)degrees * 100.0;

    if (minutes < 0.0 || minutes >= 60.0)
        return false;

    if (is_latitude)
    {
        if (degrees > 90U)
            return false;

        if (direction != 'N' && direction != 'S')
            return false;

        if (degrees == 90U && minutes > 0.0)
            return false;
    }
    else
    {
        if (degrees > 180U)
            return false;

        if (direction != 'E' && direction != 'W')
            return false;

        if (degrees == 180U && minutes > 0.0)
            return false;
    }

    double coordinate =
        (double)degrees + minutes / 60.0;

    if (direction == 'S' || direction == 'W')
        coordinate = -coordinate;

    *result = coordinate;

    return true;
}


/* =========================================================
   6. PHAN TACH CAC TRUONG DU LIEU NMEA
   ========================================================= */

static uint8_t GPS_SplitFields(char *sentence,
                               char *fields[],
                               uint8_t max_fields)
{
    uint8_t count = 0;

    if (sentence == NULL || max_fields == 0)
        return 0;

    fields[count++] = sentence;

    for (char *p = sentence; *p != '\0'; p++)
    {
        if (*p == ',')
        {
            *p = '\0';

            if (count < max_fields)
            {
                fields[count++] = p + 1;
            }
            else
            {
                break;
            }
        }
    }

    return count;
}


/* =========================================================
   7. PHAN TICH CAU RMC
   ========================================================= */

static void GPS_ParseRMC(char *fields[],
                         uint8_t count)
{
    if (count < 8U)
        return;

    /* Field 2: A = Valid, V = Invalid */
    if (fields[2][0] != 'A')
    {
        if (fields[2][0] == 'V')
        {
            gps_data.fix = false;
            gps_data.speed = 0.0f;
            gps_has_rmc = true;
            gps_last_rmc_tick = HAL_GetTick();
        }

        return;
    }

    double latitude;
    double longitude;

    if (fields[3][0] == '\0' ||
        fields[4][0] == '\0' ||
        fields[5][0] == '\0' ||
        fields[6][0] == '\0')
    {
        return;
    }

    /* Vi do */
    if (!GPS_ConvertCoordinate(
            fields[3],
            fields[4][0],
            true,
            &latitude))
    {
        return;
    }

    /* Kinh do */
    if (!GPS_ConvertCoordinate(
            fields[5],
            fields[6][0],
            false,
            &longitude))
    {
        return;
    }

    gps_data.latitude = latitude;
    gps_data.longitude = longitude;

    /* Toc do: knot -> km/h */
    double speed_knots;

    if (GPS_ParseDecimal(fields[7], &speed_knots) &&
        speed_knots >= 0.0)
    {
        gps_data.speed =
            (float)(speed_knots * 1.852);
    }

    gps_data.fix = true;
    gps_has_rmc = true;
    gps_last_rmc_tick = HAL_GetTick();
}


/* =========================================================
   8. PHAN TICH CAU GGA
   ========================================================= */

static void GPS_ParseGGA(char *fields[],
                         uint8_t count)
{
    if (count < 10U)
        return;

    uint32_t fix_quality;

    if (!GPS_ParseUnsigned(fields[6], &fix_quality))
        return;

    /* So ve tinh */
    uint32_t satellites;

    if (GPS_ParseUnsigned(fields[7], &satellites))
    {
        if (satellites <= UINT8_MAX)
        {
            gps_data.satellites =
                (uint8_t)satellites;
        }
    }

    /* Do cao chi cap nhat khi GGA co fix */
    if (fix_quality > 0U)
    {
        double altitude;

        if (GPS_ParseDecimal(fields[9], &altitude))
        {
            gps_data.altitude = (float)altitude;
        }
    }
}


/* =========================================================
   9. PHAN TICH MOT CAU NMEA
   ========================================================= */

static void GPS_ParseSentence(char *sentence)
{
    /* Bo ky tu CR o cuoi neu co */
    size_t len = strlen(sentence);

    if (len > 0 && sentence[len - 1] == '\r')
    {
        sentence[len - 1] = '\0';
    }

    /* Kiem tra checksum truoc khi sua chuoi */
    if (!GPS_Checksum(sentence))
        return;

    /* Loai bo checksum */
    char *star = strchr(sentence, '*');

    if (star == NULL)
        return;

    *star = '\0';

    /* Tach cac truong theo dau phay */
    char *fields[20];

    uint8_t count =
        GPS_SplitFields(sentence, fields, 20);

    if (count == 0)
        return;

    /* Cau RMC */
    if (strcmp(fields[0], "$GPRMC") == 0 ||
        strcmp(fields[0], "$GNRMC") == 0)
    {
        GPS_ParseRMC(fields, count);
    }

    /* Cau GGA */
    else if (strcmp(fields[0], "$GPGGA") == 0 ||
             strcmp(fields[0], "$GNGGA") == 0)
    {
        GPS_ParseGGA(fields, count);
    }
}


/* =========================================================
   10. KHOI DONG GPS DMA
   ========================================================= */

bool GPS_Init(void)
{
    HAL_StatusTypeDef status;

    /* Khong khoi dong lai DMA khi no dang nhan du lieu */
    if (gps_dma_running &&
        huart1.RxState == HAL_UART_STATE_BUSY_RX)
    {
        return true;
    }

    gps_dma_running = false;
    gps_old_pos = 0;
    gps_nmea_index = 0;
    gps_has_rmc = false;
    gps_last_rmc_tick = 0;
    gps_last_retry_tick = HAL_GetTick();

    memset(&gps_data, 0, sizeof(gps_data));
    memset(gps_dma_buffer, 0, sizeof(gps_dma_buffer));
    memset(gps_nmea_buffer, 0, sizeof(gps_nmea_buffer));

    if (huart1.hdmarx == NULL)
    {
        return false;
    }

    /* Don dep trang thai nhan cu truoc khi thu khoi dong lai. */
    if (huart1.RxState == HAL_UART_STATE_BUSY_RX ||
        huart1.ErrorCode != HAL_UART_ERROR_NONE)
    {
        if (HAL_UART_AbortReceive(&huart1) != HAL_OK)
        {
            return false;
        }
    }

    status = HAL_UART_Receive_DMA(
        &huart1,
        gps_dma_buffer,
        GPS_DMA_BUFFER_SIZE
    );

    gps_dma_running = (status == HAL_OK);
    return gps_dma_running;
}


/* =========================================================
   11. XU LY DU LIEU DMA CIRCULAR
   ========================================================= */

void GPS_Process(void)
{
    uint32_t now = HAL_GetTick();

    /* Khong dung toa do cu nhu mot GPS fix hien tai. */
    if (gps_has_rmc &&
        (uint32_t)(now - gps_last_rmc_tick) > GPS_SIGNAL_TIMEOUT_MS)
    {
        gps_data.fix = false;
        gps_data.speed = 0.0f;
        gps_data.satellites = 0;
    }

    /* Neu DMA khoi dong that bai hoac da dung, thu lai theo chu ky. */
    if (!gps_dma_running ||
        huart1.RxState != HAL_UART_STATE_BUSY_RX)
    {
        gps_dma_running = false;
        gps_data.fix = false;

        if ((uint32_t)(now - gps_last_retry_tick) >= GPS_RETRY_INTERVAL_MS)
        {
            (void)GPS_Init();
        }
        return;
    }

    if (huart1.hdmarx == NULL)
        return;

    /* Xac dinh vi tri DMA dang ghi */
    uint16_t pos =
        GPS_DMA_BUFFER_SIZE -
        __HAL_DMA_GET_COUNTER(huart1.hdmarx);

    /* DMA Circular: vi tri cuoi quay ve 0 */
    if (pos >= GPS_DMA_BUFFER_SIZE)
        pos = 0;

    /* Xu ly cac byte chua doc */
    while (gps_old_pos != pos)
    {
        char c =
            (char)gps_dma_buffer[gps_old_pos];

        gps_old_pos++;

        if (gps_old_pos >= GPS_DMA_BUFFER_SIZE)
        {
            gps_old_pos = 0;
        }

        /* Bat dau cau NMEA moi */
        if (c == '$')
        {
            gps_nmea_index = 0;

            gps_nmea_buffer[gps_nmea_index++] = c;
        }

        /* Ket thuc cau NMEA */
        else if (c == '\n')
        {
            if (gps_nmea_index > 0)
            {
                gps_nmea_buffer[gps_nmea_index] =
                    '\0';

                GPS_ParseSentence(gps_nmea_buffer);

                gps_nmea_index = 0;
            }
        }

        /* Luu cac ky tu con lai */
        else if (gps_nmea_index > 0)
        {
            if (gps_nmea_index <
                GPS_NMEA_MAX_LENGTH - 1)
            {
                gps_nmea_buffer[gps_nmea_index++] =
                    c;
            }
            else
            {
                /* Cau qua dai: loai bo */
                gps_nmea_index = 0;
            }
        }
    }
}


/* =========================================================
   12. LAY DU LIEU GPS
   ========================================================= */

bool GPS_GetData(GpsData_t *data)
{
    if (data == NULL)
        return false;

    if (!gps_has_rmc)
        return false;

    *data = gps_data;

    return true;
}