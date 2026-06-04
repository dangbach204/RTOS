#pragma once
#include "mpu6050.h"

// ─── Trạng thái phát hiện té ngã ─────────────────────
typedef enum {
    STATE_NORMAL = 0,
    STATE_FREE_FALL,
    STATE_WAIT_IMPACT,
    STATE_POST_CHECK,
    STATE_FALL_CONFIRMED
} fall_state_t;

// ─── Giai đoạn cảnh báo sau khi xác nhận té ngã ──────
typedef enum {
    ALERT_IDLE = 0,
    ALERT_WAITING,      // 0 – 15s: chờ nút hủy
    ALERT_BUZZING,      // >15s: còi đang kêu
} alert_phase_t;

/**
 * @brief Khởi tạo máy trạng thái + GPIO còi/nút.
 */
void fall_detector_init(void);

/**
 * @brief Cập nhật máy trạng thái với mẫu IMU mới.
 *        Gọi mỗi vòng lặp (~20ms).
 */
void fall_detector_update(const imu_data_t *d);

/**
 * @brief Xử lý logic còi + nút bấm.
 *        Gọi định kỳ từ button_task hoặc task riêng (~100ms).
 */
void fall_detector_alert_tick(void);

/**
 * @brief Trả về trạng thái IMU hiện tại.
 */
fall_state_t fall_detector_get_state(void);

/**
 * @brief Trả về giai đoạn cảnh báo hiện tại.
 */
alert_phase_t fall_detector_get_alert_phase(void);

/**
 * @brief Tên string của trạng thái.
 */
const char *fall_state_name(fall_state_t s);
