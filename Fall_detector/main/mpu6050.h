#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

//  Kiểu dữ liệu IMU
typedef struct
{
    float ax, ay, az; // g
    float gx, gy, gz; // deg/s
    float a_total;
    float g_total;
} imu_data_t;

//  API

/**
 * @brief Khởi động I2C bus và MPU-6050/6500.
 *        Đặt range: ±8g, ±500°/s, DLPF 21Hz.
 * @return ESP_OK nếu thành công
 */
esp_err_t mpu_init(void);

/**
 * @brief Hiệu chỉnh offset (đặt cảm biến nằm yên trước khi gọi).
 */
void mpu_calibrate(void);

/**
 * @brief Đọc 1 mẫu IMU (đã trừ offset).
 */
imu_data_t mpu_read(void);
