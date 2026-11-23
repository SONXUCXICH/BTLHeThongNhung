#ifndef ST7735_H
#define ST7735_H

#include "stm32f1xx_hal.h"

// Reset man hinh
void ST7735_Reset(void);

// Gui lenh dieu khien TFT
HAL_StatusTypeDef ST7735_WriteCommand(uint8_t cmd);

// Gui du lieu den TFT
HAL_StatusTypeDef ST7735_WriteData(
    const uint8_t *data,
    uint16_t len
);
HAL_StatusTypeDef ST7735_FillRect(
    uint16_t x, uint16_t y,
    uint16_t w, uint16_t h,
    uint16_t color
);

HAL_StatusTypeDef ST7735_DrawText(
    uint16_t x, uint16_t y,
    const char *text,
    uint16_t color,
    uint8_t scale
);
#endif
#define ST7735_WIDTH  128
#define ST7735_HEIGHT 160

// Ma mau RGB565
#define ST7735_BLACK  0x0000
#define ST7735_WHITE  0xFFFF
#define ST7735_RED    0xF800
#define ST7735_GREEN  0x07E0
#define ST7735_BLUE   0x001F

// Khoi tao TFT
HAL_StatusTypeDef ST7735_Init(void);

// To mau toan man hinh
HAL_StatusTypeDef ST7735_FillScreen(uint16_t color);