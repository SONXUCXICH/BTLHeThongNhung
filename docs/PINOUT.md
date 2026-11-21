# PINOUT – Trạm giám sát môi trường & cảnh báo an toàn công nghiệp

Vi điều khiển: **STM32F103C8T6** (ARM Cortex-M3, 72 MHz, HSE 8 MHz → PLL ×9), chạy FreeRTOS.

## 1. Bảng chân kết nối

| Ngoại vi | Giao tiếp / cấu hình | Chân STM32 | DMA / Ngắt |
|---|---|---|---|
| GPS NEO-6M | USART1, 9600 baud, NMEA | PA10 (RX) ← TX module, PA9 (TX) → RX module | DMA1_Ch5 + ngắt UART IDLE (Ring Buffer) |
| MPU6050 | I2C1 Fast Mode 400 kHz | PB6 (SCL), PB7 (SDA) | Task đọc định kỳ |
| MQ-2 (AO) | ADC1 IN0, 12-bit, 1 kênh | PA0 | DMA1_Ch1, Circular |
| Nút NEXT (chuyển trang) | GPIO Input, pull-up trong, nhấn = kéo xuống GND | PA1 | Quét 20 ms, chống dội |
| Nút PREV (chuyển trang) | GPIO Input, pull-up trong, nhấn = kéo xuống GND | PA2 | Quét 20 ms, chống dội |
| DHT11 | GPIO một dây (open-drain), polling có timeout | PB5 (đề xuất) | Không dùng ngắt |
| TFT ST7735 1.8" | SPI1 Master, chỉ TX | PA5 (SCK), PA7 (MOSI), PA4 (CS), PB0 (RES), PB1 (DC) | DMA1_Ch3 |
| Thẻ Micro SD | SPI2 Master + FATFS (FAT32) | PB13 (SCK), PB15 (MOSI), PB14 (MISO), PB12 (CS) | – |
| Còi / LED báo | TIM1 PWM hoặc GPIO | PA8 (TIM1_CH1), qua transistor | – |
| Nút E-Stop | Nút nhấn kéo xuống GND, pull-up trong | PA15 | EXTI15, ưu tiên ngắt cao nhất |
| Nạp / Debug | ST-Link (SWD) | PA13 (SWDIO), PA14 (SWCLK) | – |

## 2. Phân bổ DMA

| Kênh | Ngoại vi | Hướng | Chế độ |
|---|---|---|---|
| DMA1_Ch1 | ADC1 | Ngoại vi → RAM | Circular, Half Word |
| DMA1_Ch3 | SPI1_TX (TFT) | RAM → Ngoại vi | Normal |
| DMA1_Ch5 | USART1_RX (GPS) | Ngoại vi → RAM | Circular (Ring Buffer) |

## 3. Nguồn điện

| Mức | Cấp cho |
|---|---|
| 5 V | MQ-2 (VCC, sưởi), còi (qua transistor) |
| 3.3 V (LDO) | MCU, GPS, MPU6050, TFT, Thẻ SD, DHT11 |
| GND | Nối chung toàn mạch |

## 4. Lưu ý quan trọng

1. **MQ-2 cấp 5 V nên chân AO có thể lên tới 5 V.** Bắt buộc chia áp trước khi vào PA0 (ví dụ R trên 10 kΩ, R dưới 15 kΩ → tối đa khoảng 3.0 V). Chân ADC của STM32F103 chỉ chịu tối đa 3.3 V.
2. **PA15 mặc định là JTDI.** Phải tắt JTAG, giữ SWD (CubeMX: SYS → Debug = Serial Wire) thì mới dùng được làm EXTI15.
3. **DHT11** chưa có chân trong bảng quy hoạch ban đầu, tạm chọn PB5 (không trùng chức năng nào khác). Cần pull-up 4.7 kΩ.
4. **I2C1 (MPU6050)** cần pull-up 4.7 kΩ trên SCL và SDA. Địa chỉ 0x68 khi AD0 nối GND.
5. **Thẻ SD** khởi tạo ở tốc độ ≤ 400 kHz rồi mới tăng tốc SPI2.
6. **TFT** chỉ dùng TX nên PA6 (SPI1_MISO) để trống.
7. **SRAM chỉ 20 KB**, không chứa nổi framebuffer 128×160×16 bit (≈ 40 KB). Vẽ theo từng dải (strip) rồi đẩy bằng DMA.
8. **SWD (PA13/PA14) không dùng cho việc khác.**

## 5. Danh sách kiểm tra (Issue #4)

- [ ] Kiểm tra linh kiện và module của hệ thống
- [ ] Kiểm tra chân UART, SPI, I2C, ADC, PWM, EXTI và DMA không bị trùng
- [ ] Đo điện áp nguồn 5 V / 3.3 V; đo AO của MQ-2 sau chia áp (≤ 3.3 V)
- [ ] Kiểm tra SWD nạp và debug được qua ST-Link
- [ ] MPU6050 trả lời trên I2C1 (đọc WHO_AM_I)
- [ ] MQ-2 đổi giá trị ADC khi có khí; hai nút nhấn tạo được sự kiện NEXT/PREV
- [ ] DHT11 đọc được nhiệt độ và độ ẩm
