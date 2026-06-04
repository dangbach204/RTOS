#pragma once
#include "esp_err.h"

/**
 * @brief Khởi tạo queue + task HTTP riêng cho Blynk.
 *        GỌI TRƯỚC KHI DÙNG BẤT KỲ HÀM NÀO BÊN DƯỚI.
 *        Đặt trong app_main(), sau khi WiFi đã kết nối.
 */
void blynk_client_init(void);

/**
 * @brief Ghi giá trị số lên Blynk virtual pin.
 *        Tương đương Blynk.virtualWrite(vpin, value)
 */
esp_err_t blynk_virtual_write_float(int vpin, float value);

/**
 * @brief Ghi chuỗi lên Blynk virtual pin (tự URL-encode).
 *        Tương đương Blynk.virtualWrite(vpin, "text")
 */
esp_err_t blynk_virtual_write_str(int vpin, const char *value);

/**
 * @brief Gửi event notification lên Blynk.
 *        Tương đương Blynk.logEvent(event_name, message)
 */
esp_err_t blynk_log_event(const char *event_name, const char *message);