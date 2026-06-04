#pragma once

//  CẤU HÌNH BLYNK
#define BLYNK_TEMPLATE_ID   "TMPL6TGRICeNt"
#define BLYNK_TEMPLATE_NAME "RTOS"
#define BLYNK_AUTH_TOKEN    "2gfpS2kFmzA3qINCBjmxkuKowynUgMap"

//  CẤU HÌNH WIFI
#define WIFI_SSID     "Noobs"
#define WIFI_PASSWORD "brimstone"

//  CẤU HÌNH I2C / MPU
#define I2C_PORT      I2C_NUM_0
#define SDA_PIN       6
#define SCL_PIN       7
#define I2C_FREQ_HZ   100000
#define MPU_ADDR      0x68

// ─── MPU Registers ────────────────────────────────────
#define MPU_REG_PWR_MGMT_1  0x6B
#define MPU_REG_CONFIG      0x1A
#define MPU_REG_GYRO_CFG    0x1B
#define MPU_REG_ACCEL_CFG   0x1C
#define MPU_REG_ACCEL_XOUT  0x3B
#define MPU_REG_WHO_AM_I    0x75

//  NGƯỠNG PHÁT HIỆN TÉ NGÃ
#define FF_ABS_THRESH       0.40f
#define IMPACT_ABS_THRESH   3.00f
#define IMPACT_GYRO_THRESH  100.0f
#define DELTA_ACCEL_THRESH  2.50f
#define DELTA_GYRO_THRESH   80.0f
#define FREE_FALL_MS        80
#define IMPACT_WINDOW_MS    500
#define POST_FALL_MS        1500
#define STATIC_THRESH       0.05f
#define STATIC_BUF_SIZE     10
#define ALERT_RESET_MS      10000

// ─── Blynk Virtual Pins ───────────────────────────────
#define VPIN_STATUS   0   // V0 – trạng thái text
#define VPIN_A_TOTAL  1   // V1 – gia tốc hiện tại
#define VPIN_STATE    2   // V2 – tên state

//  CẤU HÌNH BUZZER & NÚT BẤM
#define BUZZER_PIN          10   // GPIO nối active buzzer (HIGH = kêu)
#define BUTTON_PIN          9    // GPIO nối nút bấm pull-down (HIGH khi bấm)

// Cửa sổ 5s để bấm nút hủy trước khi còi kêu
#define ALERT_CANCEL_WINDOW_MS   5000
// Sau khi còi kêu, bấm nút thì tắt và reset
// ALERT_RESET_MS (10s) vẫn dùng để tự reset nếu không ai bấm

//  TASK CONFIGURATION
#define IMU_TASK_STACK      4096
#define IMU_TASK_PRIORITY   5
#define BLYNK_TASK_STACK    6144
#define BLYNK_TASK_PRIORITY 4
#define BUTTON_TASK_STACK   4096
#define BUTTON_TASK_PRIORITY 6   // cao hơn để phản hồi nhanh

#define IMU_LOOP_MS         20    // 50 Hz
#define BLYNK_SEND_MS       500
#define SERIAL_PRINT_MS     200
#define BUTTON_DEBOUNCE_MS  50    // chống rung nút bấm