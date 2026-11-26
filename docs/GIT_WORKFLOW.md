
# QUY TRÌNH GIT - BTL HỆ THỐNG NHÚNG

## 1. Thông tin dự án
- MCU: STM32F103C8T6
- RTOS: FreeRTOS
- Repository: SONXUCXICH/BTLHeThongNhung

## 2. Phân công
- Sơn: Leader, Git, Task_GUI, Task_Alarm
- Đào: CubeMX, FreeRTOS, Task_GPS, Task_Log
- Chung: Phần cứng, Task_IMU, Task_Sensor

## 3. Cấu trúc nhánh
- main: Code ổn định đã kiểm thử.
- develop: Nhánh tích hợp code.
- feature/<ten-task>: Nhánh phát triển.
- integration/<phien-ban>: Nhánh ghép hệ thống.

## 4. Quy trình phát triển
1. Cập nhật develop mới nhất.
2. Tạo feature branch từ develop.
3. Viết code và kiểm thử độc lập.
4. Commit và push lên GitHub.
5. Mở Pull Request vào develop.
6. Thành viên khác review, Sơn quản lý merge.
7. Xóa feature branch sau khi merge.

## 5. Quy ước tên nhánh
- feature/project-setup
- feature/hal-uart-spi2
- feature/hal-i2c-adc
- feature/hal-spi1-timer
- feature/task-gps
- feature/task-log
- feature/task-imu
- feature/task-sensor
- feature/task-gui
- feature/task-alarm

## 6. Quy tắc commit
- feat: Thêm chức năng
- fix: Sửa lỗi
- docs: Tài liệu
- chore: Cấu hình chung
- refactor: Tối ưu mã nguồn

Ví dụ:
- feat(task-gui): implement TFT display
- fix(task-gps): handle UART timeout
- docs: update pinout

## 7. Quy tắc file chung
- Đào phụ trách cấu hình CubeMX/FreeRTOS.
- Không tự ý sửa main.c hoặc các file IPC chung.
- Thay đổi API phải được nhóm thống nhất.
- Không commit thư mục build/ hoặc Debug/.
- Kiểm thử trước khi mở Pull Request.

## 8. Tích hợp và phát hành
1. Merge các task hoàn thành vào develop.
2. Tạo integration/system-v1 từ develop.
3. Ghép các task và cơ chế IPC.
4. Kiểm thử hệ thống trên kit.
5. Merge integration về develop.
6. Merge develop vào main khi ổn định.
7. Tạo tag v1.0.0.
  
