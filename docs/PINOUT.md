# PINOUT – Trạm giám sát môi trường & cảnh báo an toàn công nghiệp

Vi điều khiển: **STM32F103C8T6** (ARM Cortex-M3, 72 MHz, HSE 8 MHz → PLL ×9), chạy FreeRTOS.

## 1. Bảng chân kết nối

| Ngoại vi | Giao tiếp / cấu hình | Chân STM32 | DMA / Ngắt |
|---|---|---|---|
| GPS NEO-6M | USART1, 9600 baud, NMEA | PA10 (RX) ← TX module, PA9 (TX) → RX module | DMA1_Ch5 + ngắt UART IDLE (Ring Buffer) |
| MMA845x (gia tốc kế) | I2C1 Fast Mode 400 kHz | PB6 (SCL), PB7 (SDA) | Task đọc định kỳ |
| MQ-2 (AO) | ADC1 IN0, 12-bit, Scan 3 kênh | PA0 | DMA1_Ch1, Circular |
| Joystick VRx | ADC1 IN1 (cùng bộ Scan) | PA1 | DMA1_Ch1, Circular |
| Joystick VRy | ADC1 IN2 (cùng bộ Scan) | PA2 | DMA1_Ch1, Circular |
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
| 3.3 V (LDO) | MCU, GPS, MMA845x, TFT, Thẻ SD, DHT11, Joystick |
| GND | Nối chung toàn mạch |

## 4. Đấu dây theo chân module

| Linh kiện | Chân trên module | Nối vào |
|---|---|---|
| MMA845x | VCC-IN / GND | 3.3 V / GND (chân 3.3V để trống) |
| | SCL / SDA | PB6 / PB7 |
| | SA0 | GND (địa chỉ 0x1C) hoặc 3.3 V (0x1D), không để hở |
| | INT1, INT2 | Để trống |
| MQ-2 | VCC / GND | 5 V / GND |
| | AO | Qua cầu chia áp (10 kΩ trên, 15 kΩ dưới) rồi vào PA0 |
| | DO | Để trống |
| Joystick | VCC / GND | 3.3 V / GND |
| | VRx / VRy | PA1 / PA2 |
| | SW | Để trống (chưa dùng) |
| DHT11 | VCC / GND | 3.3 V / GND |
| | DATA | PB5 (pull-up 4.7 kΩ lên 3.3 V) |
| GPS NEO-6M | VCC / GND | 3.3 V / GND |
| | TX / RX | PA10 / PA9 (nối chéo) |
| TFT ST7735 | VCC / GND / LED | 3.3 V / GND / 3.3 V |
| | SCK / SDA (MOSI) | PA5 / PA7 |
| | CS / RES / DC | PA4 / PB0 / PB1 |
| Thẻ Micro SD (module SPI) | VCC / GND | 3.3 V (hoặc 5 V nếu module có ổn áp) / GND |
| | SCK / MOSI / MISO / CS | PB13 / PB15 / PB14 / PB12 |
| Còi / LED báo | Điều khiển | PA8 qua transistor |
| Nút E-Stop | Hai đầu | PA15 và GND |
| ST-Link | SWDIO / SWCLK / GND | PA13 / PA14 / GND |

## 5. Lưu ý quan trọng

1. **MQ-2 cấp 5 V nên chân AO có thể lên tới 5 V.** Bắt buộc chia áp trước khi vào PA0 (ví dụ R trên 10 kΩ, R dưới 15 kΩ → tối đa khoảng 3.0 V). Chân ADC của STM32F103 chỉ chịu tối đa 3.3 V.
2. **PA15 mặc định là JTDI.** Phải tắt JTAG, giữ SWD (CubeMX: SYS → Debug = Serial Wire) thì mới dùng được làm EXTI15.
3. **DHT11** chưa có chân trong bảng quy hoạch ban đầu, tạm chọn PB5 (không trùng chức năng nào khác). Cần pull-up 4.7 kΩ.
4. **I2C1 (MMA845x)** cần pull-up 4.7 kΩ trên SCL và SDA. Địa chỉ 0x1C khi SA0 nối GND, 0x1D khi SA0 nối 3.3 V (code tự dò).
5. **Thẻ SD** khởi tạo ở tốc độ ≤ 400 kHz rồi mới tăng tốc SPI2.
6. **TFT** chỉ dùng TX nên PA6 (SPI1_MISO) để trống.
7. **SRAM chỉ 20 KB**, không chứa nổi framebuffer 128×160×16 bit (≈ 40 KB). Vẽ theo từng dải (strip) rồi đẩy bằng DMA.
8. **SWD (PA13/PA14) không dùng cho việc khác.**
9. **Joystick cấp 3.3 V, không cấp 5 V**, vì 5 V làm quá áp chân ADC của STM32.
10. **MMA845x chỉ có gia tốc kế (không có gyro)**, nên góc Pitch/Roll tính từ gia tốc rồi lọc IIR, không dùng Complementary Filter.
11. **Module thẻ SD**: kiểm tra chữ in trên module để biết cấp 3.3 V hay 5 V. Thẻ dùng dung lượng ≤ 32 GB, FAT32.

## 6. Danh sách kiểm tra (Issue #4)

- [ ] Kiểm tra linh kiện và module của hệ thống
- [ ] Kiểm tra chân UART, SPI, I2C, ADC, PWM, EXTI và DMA không bị trùng
- [ ] Đo điện áp nguồn 5 V / 3.3 V; đo AO của MQ-2 sau chia áp (≤ 3.3 V)
- [ ] Kiểm tra SWD nạp và debug được qua ST-Link
- [ ] MMA845x trả lời trên I2C1 (đọc WHO_AM_I: 0x1A = MMA8451, 0x2A = MMA8452, 0x3A = MMA8453)
- [ ] MQ-2 và Joystick đổi giá trị ADC khi tác động
- [ ] DHT11 đọc được nhiệt độ và độ ẩm
