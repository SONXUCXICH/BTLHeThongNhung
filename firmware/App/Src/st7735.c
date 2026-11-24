#include "st7735.h"
#include "main.h"
#include <stddef.h>

// SPI1 da duoc Dao cau hinh
extern SPI_HandleTypeDef hspi1;

// Gui mot lenh den man hinh
HAL_StatusTypeDef ST7735_WriteCommand(uint8_t cmd)
{
    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,
                      TFT_CS_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port,
                      TFT_DC_Pin, GPIO_PIN_RESET);

    HAL_StatusTypeDef status =
        HAL_SPI_Transmit(&hspi1, &cmd, 1, 100);

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,
                      TFT_CS_Pin, GPIO_PIN_SET);

    return status;
}

// Gui du lieu den man hinh
HAL_StatusTypeDef ST7735_WriteData(
    const uint8_t *data, uint16_t len)
{
    if (len == 0) return HAL_OK;
    if (data == NULL) return HAL_ERROR;

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,
                      TFT_CS_Pin, GPIO_PIN_RESET);

    HAL_GPIO_WritePin(TFT_DC_GPIO_Port,
                      TFT_DC_Pin, GPIO_PIN_SET);

    HAL_StatusTypeDef status =
        HAL_SPI_Transmit(&hspi1,
                         (uint8_t *)data,
                         len, 100);

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,
                      TFT_CS_Pin, GPIO_PIN_SET);

    return status;
}

// Reset phan cung man hinh
void ST7735_Reset(void)
{
    HAL_GPIO_WritePin(TFT_RES_GPIO_Port,
                      TFT_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(10);

    HAL_GPIO_WritePin(TFT_RES_GPIO_Port,
                      TFT_RES_Pin, GPIO_PIN_RESET);
    HAL_Delay(20);

    HAL_GPIO_WritePin(TFT_RES_GPIO_Port,
                      TFT_RES_Pin, GPIO_PIN_SET);
    HAL_Delay(120);
}
HAL_StatusTypeDef ST7735_Init(void)
{
    uint8_t data;

    // 1. Reset phan cung
    ST7735_Reset();

    // 2. Software reset
    if (ST7735_WriteCommand(0x01) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(150);

    // 3. Thoat che do Sleep
    if (ST7735_WriteCommand(0x11) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(150);

    // 4. Mau 16-bit RGB565
    if (ST7735_WriteCommand(0x3A) != HAL_OK)
        return HAL_ERROR;

    data = 0x05;
    if (ST7735_WriteData(&data, 1) != HAL_OK)
        return HAL_ERROR;

    // 5. Huong hien thi mac dinh
    if (ST7735_WriteCommand(0x36) != HAL_OK)
        return HAL_ERROR;

    data = 0x00;
    if (ST7735_WriteData(&data, 1) != HAL_OK)
        return HAL_ERROR;

    // 6. Normal display mode
    if (ST7735_WriteCommand(0x13) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(10);

    // 7. Bat man hinh
    if (ST7735_WriteCommand(0x29) != HAL_OK)
        return HAL_ERROR;
    HAL_Delay(100);

    return HAL_OK;
}
HAL_StatusTypeDef ST7735_FillScreen(uint16_t color)
{
    uint8_t col[] = {0, 0, 0, ST7735_WIDTH - 1};
    uint8_t row[] = {0, 0, 0, ST7735_HEIGHT - 1};
    uint8_t buffer[128];

    // Chuan bi 64 pixel RGB565
    for (int i = 0; i < 64; i++)
    {
        buffer[2 * i] = color >> 8;
        buffer[2 * i + 1] = color & 0xFF;
    }

    // Thiet lap cot 0..127
    if (ST7735_WriteCommand(0x2A) != HAL_OK)
        return HAL_ERROR;
    if (ST7735_WriteData(col, 4) != HAL_OK)
        return HAL_ERROR;

    // Thiet lap hang 0..159
    if (ST7735_WriteCommand(0x2B) != HAL_OK)
        return HAL_ERROR;
    if (ST7735_WriteData(row, 4) != HAL_OK)
        return HAL_ERROR;

    // Bat dau ghi pixel
    if (ST7735_WriteCommand(0x2C) != HAL_OK)
        return HAL_ERROR;

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,
                      TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(TFT_DC_GPIO_Port,
                      TFT_DC_Pin, GPIO_PIN_SET);

    HAL_StatusTypeDef status = HAL_OK;

    // 128 x 160 = 20480 pixel
    // Moi lan truyen 64 pixel
    for (int i = 0; i < 320; i++)
    {
        status = HAL_SPI_Transmit(&hspi1,
                                  buffer, 128, 100);
        if (status != HAL_OK)
            break;
    }

    HAL_GPIO_WritePin(TFT_CS_GPIO_Port,
                      TFT_CS_Pin, GPIO_PIN_SET);

    return status;
}

/* Chon vung hien thi */
static HAL_StatusTypeDef ST7735_SetWindow(
    uint16_t x0, uint16_t y0,
    uint16_t x1, uint16_t y1)
{
    uint8_t d[4];

    if (ST7735_WriteCommand(0x2A) != HAL_OK)
        return HAL_ERROR;

    d[0] = x0 >> 8;
    d[1] = x0 & 0xFF;
    d[2] = x1 >> 8;
    d[3] = x1 & 0xFF;

    if (ST7735_WriteData(d, 4) != HAL_OK)
        return HAL_ERROR;

    if (ST7735_WriteCommand(0x2B) != HAL_OK)
        return HAL_ERROR;

    d[0] = y0 >> 8;
    d[1] = y0 & 0xFF;
    d[2] = y1 >> 8;
    d[3] = y1 & 0xFF;

    return ST7735_WriteData(d, 4);
}

/* Ve hinh chu nhat */
HAL_StatusTypeDef ST7735_FillRect(
    uint16_t x, uint16_t y,
    uint16_t w, uint16_t h,
    uint16_t color)
{
    if (x >= ST7735_WIDTH ||
        y >= ST7735_HEIGHT ||
        w == 0 || h == 0)
        return HAL_ERROR;

    if ((uint32_t)x + w > ST7735_WIDTH)
        w = ST7735_WIDTH - x;

    if ((uint32_t)y + h > ST7735_HEIGHT)
        h = ST7735_HEIGHT - y;

    if (ST7735_SetWindow(
            x, y, x + w - 1, y + h - 1) != HAL_OK)
        return HAL_ERROR;

    if (ST7735_WriteCommand(0x2C) != HAL_OK)
        return HAL_ERROR;

    uint8_t buffer[64];

    for (int i = 0; i < 32; i++)
    {
        buffer[i * 2] = color >> 8;
        buffer[i * 2 + 1] = color & 0xFF;
    }

    uint32_t remaining = (uint32_t)w * h;

    HAL_GPIO_WritePin(
        TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(
        TFT_DC_GPIO_Port, TFT_DC_Pin, GPIO_PIN_SET);

    HAL_StatusTypeDef status = HAL_OK;

    while (remaining > 0)
    {
        uint16_t count =
            remaining > 32 ? 32 : (uint16_t)remaining;

        status = HAL_SPI_Transmit(
            &hspi1, buffer, count * 2, 100);

        if (status != HAL_OK)
            break;

        remaining -= count;
    }

    HAL_GPIO_WritePin(
        TFT_CS_GPIO_Port, TFT_CS_Pin, GPIO_PIN_SET);

    return status;
}

/* Font 5x7 thu nghiem cho chu STM32 OK */
static const uint8_t *ST7735_GetGlyph(char c)
{
    static const uint8_t S[5] =
        {0x46, 0x49, 0x49, 0x49, 0x31};
    static const uint8_t T[5] =
        {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t M[5] =
        {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t C3[5] =
        {0x22, 0x41, 0x49, 0x49, 0x36};
    static const uint8_t C2[5] =
        {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t O[5] =
        {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t K[5] =
        {0x7F, 0x08, 0x14, 0x22, 0x41};

    static const uint8_t N0[5] = {0x3E,0x51,0x49,0x45,0x3E};
    static const uint8_t N1[5] = {0x00,0x42,0x7F,0x40,0x00};
    static const uint8_t N4[5] = {0x18,0x14,0x12,0x7F,0x10};
    static const uint8_t N5[5] = {0x27,0x45,0x45,0x45,0x39};
    static const uint8_t N6[5] = {0x3C,0x4A,0x49,0x49,0x30};
    static const uint8_t N7[5] = {0x01,0x71,0x09,0x05,0x03};
    static const uint8_t N8[5] = {0x36,0x49,0x49,0x49,0x36};
    static const uint8_t N9[5] = {0x06,0x49,0x49,0x29,0x1E};


    switch (c)
    {
        case 'S': return S;
        case 'T': return T;
        case 'M': return M;
        case '3': return C3;
        case '2': return C2;
        case 'O': return O;
        case 'K': return K;

        // Cac chu so moi bo sung
        case '0': return N0;
        case '1': return N1;
        case '4': return N4;
        case '5': return N5;
        case '6': return N6;
        case '7': return N7;
        case '8': return N8;
        case '9': return N9;

        default: return NULL;
    }

}

/* Hien thi chuoi ky tu */
HAL_StatusTypeDef ST7735_DrawText(
    uint16_t x, uint16_t y,
    const char *text,
    uint16_t color,
    uint8_t scale)
{
    if (text == NULL || scale == 0)
        return HAL_ERROR;

    while (*text)
    {
        if (*text == ' ')
        {
            x += 6 * scale;
            text++;
            continue;
        }

        const uint8_t *glyph = ST7735_GetGlyph(*text);

        if (glyph == NULL)
            return HAL_ERROR;

        for (int col = 0; col < 5; col++)
        {
            for (int row = 0; row < 7; row++)
            {
                if (glyph[col] & (1U << row))
                {
                    if (ST7735_FillRect(
                        x + col * scale,
                        y + row * scale,
                        scale, scale,
                        color) != HAL_OK)
                        return HAL_ERROR;
                }
            }
        }

        x += 6 * scale;
        text++;
    }

    return HAL_OK;
}
