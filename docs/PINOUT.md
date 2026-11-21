# PINOUT – Trạm giám sát môi trường & cảnh báo an toàn công nghiệp

Vi điều khiển: **STM32F103C8T6** (ARM Cortex-M3, mục tiêu 72 MHz; nếu sử dụng thạch anh HSE 8 MHz thì cấu hình PLL ×9), chạy **FreeRTOS**.

> **Trạng thái:** Bản PINOUT đã cập nhật theo phần cứng nhóm đang sử dụng (**Joystick PS2** và **nút E-Stop tiếp điểm NO**). Chung, Sơn và Đào cần đối chiếu lần cuối với module thực tế trước khi chốt file `.ioc` trong STM32CubeMX. **Đào là người phụ trách cấu hình/generate code CubeMX chung.**

## 1. Bảng chân kết nối

| Ngoại vi | Giao tiếp / cấu hình | Chân STM32 | DMA / Ngắt |
|---|---|---|---|
| GPS NEO-6M | USART1, 9600 baud, NMEA | PA10 (USART1_RX) ← TX của GPS; PA9 (USART1_TX) → RX của GPS (nếu cần cấu hình) | DMA1_Ch5 (USART1_RX) + ngắt UART IDLE, ring buffer |
| MPU6050 | I2C1, dự kiến Fast Mode 400 kHz | PB6 (SCL), PB7 (SDA) | Đọc theo chu kỳ trong `Task_IMU` |
| MQ-2 (AO) | ADC1_IN0, 12-bit; **kênh 1/3** trong ADC Scan | PA0 (sau mạch chia áp) | DMA1_Ch1, Circular |
| **Joystick PS2 – VRx** | **ADC1_IN1, 12-bit; kênh 2/3** trong ADC Scan | **PA1** | **DMA1_Ch1, Circular (dùng chung bộ đệm ADC)** |
| **Joystick PS2 – VRy** | **ADC1_IN2, 12-bit; kênh 3/3** trong ADC Scan | **PA2** | **DMA1_Ch1, Circular (dùng chung bộ đệm ADC)** |
| Joystick PS2 – SW | Nút nhấn tích hợp (tùy chọn) | **Chưa phân bổ**, có thể để không nối | Chưa sử dụng trong yêu cầu chuyển trang bằng 4 hướng |
| DHT11 | GPIO một dây, cần chuyển hướng chân khi giao tiếp; đọc có timeout | PB5 (dự kiến) | Không dùng DMA; đọc định kỳ, tối thiểu 2 s/lần |
| TFT ST7735 1.8\" | SPI1 Master, truyền 8-bit; chân `SDA` trên TFT là MOSI, `A0` là DC | PA5 (SCK), PA7 (MOSI), PA4 (CS), PB0 (RESET), PB1 (DC/A0) | DMA1_Ch3 (SPI1_TX) |
| Thẻ MicroSD | SPI2 Master + FatFs (FAT32) | PB13 (SCK), PB15 (MOSI), PB14 (MISO), PB12 (CS) | Khởi tạo ở tốc độ thấp; driver lưu trữ do Đào phụ trách |
| Còi cảnh báo | TIM1_CH1 PWM (nếu dùng còi thụ động), tầng đệm/transistor phù hợp | PA8 | TIM1 PWM |
| LED cảnh báo (nếu lắp riêng) | GPIO Output | **Chưa phân bổ chân** | Cần chốt nếu sử dụng LED rời |
| **E-Stop (tiếp điểm NO)** | **PA15 nối qua nút NO xuống GND; GPIO Input Pull-up; EXTI cạnh xuống (Falling Edge)** | **PA15 (EXTI15)** | `EXTI15_10_IRQn`, thông báo sự kiện bằng API `...FromISR()` phù hợp |
| Nạp / Debug | ST-Link (SWD) | PA13 (SWDIO), PA14 (SWCLK) | Giữ SWD, tắt JTAG để dùng PA15 |

**Ghi chú về Joystick:** Không sử dụng hai nút NEXT/PREV trên PA1/PA2 như bản cũ. Chung xử lý điện áp `VRx`, `VRy` thành sự kiện `UP`, `DOWN`, `LEFT`, `RIGHT` và gửi qua Queue `qJoy`; Sơn nhận các sự kiện đó trong `Task_GUI` để chuyển trang. Chiều tương ứng với giá trị ADC phải xác nhận bằng thử nghiệm thực tế.

## 2. Phân bổ DMA và ADC

| Kênh DMA | Ngoại vi | Hướng | Chế độ / bộ đệm |
|---|---|---|---|
| DMA1_Ch1 | **ADC1 – 3 kênh: IN0 (PA0), IN1 (PA1), IN2 (PA2)** | Ngoại vi → RAM | **Circular**, Half Word, `uint16_t adcBuf[3]` |
| DMA1_Ch3 | SPI1_TX (TFT) | RAM → Ngoại vi | Normal (truyền theo dải) |
| DMA1_Ch5 | USART1_RX (GPS) | Ngoại vi → RAM | Circular (ring buffer) |

**Yêu cầu cấu hình ADC1 cho Đào:**

- Độ phân giải **12-bit**; bật **Scan Conversion** với thứ tự rank **IN0 → IN1 → IN2** và **3 conversions**.
- Dùng **DMA Circular**, bộ đệm 3 phần tử `adcBuf[0] = MQ-2`, `adcBuf[1] = VRx`, `adcBuf[2] = VRy`.
- Có thể sử dụng Continuous Conversion theo kế hoạch; bảo đảm tốc độ ADC không vượt giới hạn của STM32F103 (ví dụ clock ADC 12 MHz khi APB2 = 72 MHz và prescaler /6).
- Không cấu hình PA1/PA2 thành GPIO Input cho hai nút NEXT/PREV.
- Kiểm tra các tên tùy chọn Scan/DMA trong phiên bản STM32CubeMX thực tế trước khi generate.

## 3. Nguồn điện

| Mức | Cấp cho / ghi chú |
|---|---|
| 5 V | MQ-2 (mạch sưởi); còi nếu loại còi yêu cầu 5 V và có tầng điều khiển phù hợp |
| 3.3 V | MCU; **Joystick PS2** (để VRx/VRy không vượt 3.3 V); TFT theo thông số module; MPU6050, thẻ SD, DHT11/GPS **chỉ khi module tương thích mức nguồn này** |
| GND | Tất cả các module dùng chung GND với STM32 |

**Chú ý:** Xác nhận điện áp cấp và điện áp logic của **từng board module thực tế** (đặc biệt là GPS, TFT, DHT11 và khe MicroSD). Không cấp trực tiếp tín hiệu 5 V vào chân ADC STM32. Trước khi bật nguồn, kiểm tra tổng dòng tiêu thụ có phù hợp bộ nguồn 3.3 V/5 V đang sử dụng.

## 4. Lưu ý quan trọng

1. **MQ-2 dùng 5 V nên chân AO có thể cao hơn 3.3 V.** Phải chia áp trước khi vào PA0; ví dụ R trên 10 kΩ, R dưới 15 kΩ cho điện áp lý tưởng 5 V xuống khoảng **3.0 V**. Đo bằng đồng hồ để xác nhận điện áp ở PA0 không vượt 3.3 V.
2. **Joystick PS2:** Cấp nguồn **3.3 V**, GND chung; `VRx → PA1`, `VRy → PA2`. Hai trục là **tín hiệu analog**, không phải nút nhấn NEXT/PREV. Nút `SW` tạm không sử dụng và không tự ý chiếm chân MCU khác.
3. **E-Stop NO:** Nối **PA15 — nút NO — GND**; cấu hình **Pull-up + Falling Edge**. Bình thường PA15 = 1, nhấn = 0. Phần mềm cần chống dội và **giữ trạng thái cảnh báo (latch)** cho đến khi có thao tác xóa/xác nhận rõ ràng; không tự xóa cảnh báo chỉ vì thả nút.
4. **PA15 mặc định thuộc JTAG (JTDI).** Trong CubeMX chọn **SYS → Debug = Serial Wire** để tắt JTAG, vẫn giữ PA13/PA14 cho SWD.
5. **Ưu tiên ngắt FreeRTOS:** IRQ `EXTI15_10_IRQn`, UART IDLE và DMA nếu sử dụng API `...FromISR()` phải có mức ưu tiên NVIC **hợp lệ đối với `configMAX_SYSCALL_INTERRUPT_PRIORITY`** (giá trị ưu tiên số không được cao hơn ngưỡng cho phép). Không tùy ý đặt IRQ E-Stop mức ưu tiên số 0 rồi gọi API FreeRTOS. Thống nhất NVIC priority grouping với Đào.
6. **DHT11:** PB5 là chân dự kiến; cần kiểm tra điện trở kéo lên (thường khoảng 4.7 kΩ đến nguồn logic phù hợp). Đọc tối thiểu cách nhau 2 s và phải có timeout, tránh giữ ngắt bị vô hiệu quá lâu.
7. **MPU6050:** I2C1 cần điện trở kéo lên SCL/SDA về mức logic **3.3 V** (module có thể đã có pull-up). Địa chỉ I2C thường là `0x68` nếu AD0 nối GND; kiểm tra `WHO_AM_I` trước khi viết bộ lọc góc.
8. **MicroSD:** Khởi tạo SPI ở tốc độ **≤ 400 kHz**, sau đó tăng theo khả năng thực tế. Có thể dùng **khe MicroSD ở mặt sau TFT** nếu xác nhận bốn chân SD riêng trên board và dây nối phù hợp với SPI2; việc này thuộc `Task_Log` của **Đào**. Dùng **thẻ microSD**, không phải ổ SSD; ưu tiên thẻ microSDHC định dạng FAT32 để thử ban đầu.
9. **TFT:** Giao tiếp SPI1 chỉ truyền nên PA6 (SPI1_MISO) để trống. Chân `LED` đèn nền phải nối đúng điện áp/kiểu điều khiển của module. STM32F103C8T6 có SRAM danh định **20 KB**, không chứa được toàn bộ framebuffer 128×160×16 bit (**40,960 byte**); nên vẽ theo dải nhỏ và truyền bằng DMA.
10. **Còi:** Chốt loại còi chủ động/thụ động trước khi dùng PWM, chọn tầng transistor/diode bảo vệ nếu tải yêu cầu. Không cấp tải còi trực tiếp từ chân GPIO khi vượt khả năng dòng của MCU.
11. **E-Stop trong đồ án là tín hiệu mô phỏng chức năng cảnh báo**, không phải mạch dừng khẩn cấp an toàn đạt chuẩn cho thiết bị công nghiệp thật.
12. **Quản lý cấu hình chung:** Chỉ **Đào** cập nhật/generate file `.ioc` chung; Chung phụ trách đọc cảm biến và Joystick; Sơn phụ trách TFT, còi, E-Stop và review Pull Request. Thay đổi chân cần cả nhóm đồng ý trước khi merge vào `develop`.

## 5. Danh sách kiểm tra (Issue #4)

- [ ] Kiểm tra thực tế các module, điện áp nguồn 5 V / 3.3 V và GND chung.
- [ ] Đối chiếu toàn bộ UART, SPI1, SPI2, I2C1, ADC1, PWM, EXTI, SWD và các kênh DMA; xử lý chân **chưa phân bổ** nếu cần.
- [ ] Đo tín hiệu MQ-2 sau chia áp để xác nhận PA0 không vượt 3.3 V.
- [ ] Xác nhận cấp nguồn Joystick **3.3 V**; đo ADC VRx/VRy tại vị trí giữa và bốn hướng; kiểm tra chuyển thành `JoyEvent_t` đúng chiều.
- [ ] Cấu hình ADC1 **3 kênh** với bộ đệm `adcBuf[3]`, DMA Circular; không dùng PA1/PA2 làm nút GPIO.
- [ ] Kiểm tra nút E-Stop **NO**, PA15 Pull-up và ngắt Falling Edge; xác minh đã tắt JTAG nhưng SWD vẫn nạp/debug được.
- [ ] Kiểm tra `EXTI15_10_IRQn` và các IRQ có dùng `...FromISR()` tương thích cấu hình ưu tiên FreeRTOS.
- [ ] MPU6050 trả lời trên I2C1 (`WHO_AM_I`) và các giá trị đo thay đổi hợp lý.
- [ ] DHT11 đọc được nhiệt độ/độ ẩm, xử lý timeout khi ngắt kết nối.
- [ ] Kiểm tra thực tế chân TFT/ST7735 và khe MicroSD nếu nhóm muốn dùng khe SD tích hợp.
- [ ] **Đào và Sơn duyệt PINOUT**, sau đó Chung cập nhật Pull Request `feature/docs-pinout` → `develop` để Sơn merge.
