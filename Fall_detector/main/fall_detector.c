#include "fall_detector.h"
#include "blynk_client.h"
#include "buzzer.h"
#include "button.h"
#include "config.h"

#include <math.h>
#include <string.h>
#include <stdint.h>
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "FALL";

//  Macro thay millis()
static inline uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

//  State machine – phát hiện té ngã
static fall_state_t fall_state   = STATE_NORMAL;
static uint32_t     state_timer  = 0;
static float        prev_a_total = 1.0f;
static float        prev_g_total = 0.0f;

// ─── Static buffer (stddev) ───────────────────────────
static float   static_buf[STATIC_BUF_SIZE];
static uint8_t static_idx  = 0;
static bool    static_full = false;

static void reset_static_buf(void)
{
    static_idx  = 0;
    static_full = false;
}

static void push_static(float val)
{
    static_buf[static_idx] = val;
    static_idx = (static_idx + 1) % STATIC_BUF_SIZE;
    if (static_idx == 0) static_full = true;
}

static float calc_std_dev(void)
{
    uint8_t n = static_full ? STATIC_BUF_SIZE : static_idx;
    if (n < 2) return 999.0f;
    float sum = 0, sum_sq = 0;
    for (uint8_t i = 0; i < n; i++) {
        sum    += static_buf[i];
        sum_sq += static_buf[i] * static_buf[i];
    }
    float mean = sum / n;
    float var  = (sum_sq / n) - (mean * mean);
    return sqrtf(var > 0 ? var : 0);
}

//  Alert phase – còi + nút
// Sơ đồ:
//
//  Té ngã xác nhận
//       │
//       ▼
//  ALERT_WAITING ──── bấm nút (< 15s) ──► reset (IDLE)
//       │
//    quá 15s
//       │
//       ▼
//  ALERT_BUZZING ──── bấm nút ──────────► reset (IDLE)
//       │
//    quá ALERT_RESET_MS không bấm
//       │
//       ▼
//    tự reset (IDLE)

static alert_phase_t alert_phase  = ALERT_IDLE;
static uint32_t      alert_timer  = 0;   // thời điểm bắt đầu ALERT_WAITING

// Thời gian bỏ qua nút ngay sau khi xác nhận té ngã (tránh nhiễu)
#define BUTTON_IGNORE_MS   2000   // 2s đầu không nhận nút

// Gọi khi té ngã mới được xác nhận
static void alert_start(void)
{
    alert_phase = ALERT_WAITING;
    alert_timer = millis();
    // Flush pressed_flag cũ để tránh nút bị đọc nhầm từ trước
    button_was_pressed();
    ESP_LOGW(TAG, "[ALERT] Bat dau dem 15s - bam nut de huy! (bo qua nut %dms dau)", BUTTON_IGNORE_MS);
}

// Reset toàn bộ alert về IDLE
static void alert_reset(const char *reason)
{
    ESP_LOGI(TAG, "[ALERT] Reset: %s", reason);
    buzzer_off();
    alert_phase = ALERT_IDLE;
    blynk_virtual_write_str(VPIN_STATUS, "OK - Binh thuong");
    fall_state = STATE_NORMAL;
}

// ─── Callback khi xác nhận té ngã (gửi Blynk) ────────
static void on_fall_detected(void)
{
    ESP_LOGW(TAG, "╔══════════════════════════════╗");
    ESP_LOGW(TAG, "║  !!! TE NGA XAC NHAN !!!     ║");
    ESP_LOGW(TAG, "╚══════════════════════════════╝");

    blynk_log_event("alert", "Phat hien te nga! Kiem tra ngay!");
    blynk_virtual_write_str(VPIN_STATUS, "TE NGA!");
    blynk_virtual_write_str(VPIN_STATE,  "CONFIRMED");

    alert_start();
}

//  Public API – init
void fall_detector_init(void)
{
    fall_state   = STATE_NORMAL;
    state_timer  = millis();
    prev_a_total = 1.0f;
    prev_g_total = 0.0f;
    alert_phase  = ALERT_IDLE;
    reset_static_buf();

    buzzer_init();
    button_init();
}

//  Public API – cập nhật IMU (50Hz)
void fall_detector_update(const imu_data_t *d)
{
    uint32_t now   = millis();
    float delta_a  = fabsf(d->a_total - prev_a_total);
    float delta_g  = fabsf(d->g_total - prev_g_total);
    bool big_delta = (delta_a > DELTA_ACCEL_THRESH) || (delta_g > DELTA_GYRO_THRESH);
    bool free_fall = (d->a_total < FF_ABS_THRESH);
    bool impact    = (d->a_total > IMPACT_ABS_THRESH) || (d->g_total > IMPACT_GYRO_THRESH);

    switch (fall_state) {

        case STATE_NORMAL:
            if (free_fall || big_delta) {
                fall_state  = STATE_FREE_FALL;
                state_timer = now;
                ESP_LOGI(TAG, "[1/FF ] a=%.3fg  da=%.3fg  g=%.1f",
                         d->a_total, delta_a, d->g_total);
            }
            break;

        case STATE_FREE_FALL:
            if (impact) {
                fall_state  = STATE_WAIT_IMPACT;
                state_timer = now - IMPACT_WINDOW_MS + 1;
                ESP_LOGI(TAG, "[2/FF ] Impact som!");
                break;
            }
            if (!free_fall && !big_delta && (now - state_timer < FREE_FALL_MS)) {
                ESP_LOGI(TAG, "[2/FF ] Huy - phuc hoi nhanh.");
                fall_state = STATE_NORMAL;
                break;
            }
            if ((now - state_timer >= FREE_FALL_MS) || big_delta) {
                fall_state  = STATE_WAIT_IMPACT;
                state_timer = now;
                ESP_LOGI(TAG, "[2/FF ] Du dieu kien -> cho impact");
            }
            break;

        case STATE_WAIT_IMPACT:
            if (impact) {
                fall_state  = STATE_POST_CHECK;
                state_timer = now;
                reset_static_buf();
                ESP_LOGI(TAG, "[3/IMP] Va cham! a=%.2fg  g=%.1f",
                         d->a_total, d->g_total);
            } else if (now - state_timer > IMPACT_WINDOW_MS) {
                ESP_LOGI(TAG, "[3/IMP] Timeout.");
                fall_state = STATE_NORMAL;
            }
            break;

        case STATE_POST_CHECK:
            push_static(d->a_total);
            if (now - state_timer >= POST_FALL_MS) {
                float sd = calc_std_dev();
                ESP_LOGI(TAG, "[4/CHK] Std-dev=%.4f (nguong %.2f)", sd, STATIC_THRESH);
                if (sd < STATIC_THRESH) {
                    fall_state  = STATE_FALL_CONFIRMED;
                    state_timer = now;
                    on_fall_detected();   // → bắt đầu alert
                } else {
                    ESP_LOGI(TAG, "[4/CHK] Con chuyen dong - huy.");
                    fall_state = STATE_NORMAL;
                }
            }
            break;

        case STATE_FALL_CONFIRMED:
            // Trạng thái này chỉ thoát qua alert_reset()
            // (được gọi từ fall_detector_alert_tick)
            break;
    }

    prev_a_total = d->a_total;
    prev_g_total = d->g_total;
}

//  Public API – xử lý còi + nút (~100ms)
void fall_detector_alert_tick(void)
{
    if (alert_phase == ALERT_IDLE) return;

    uint32_t elapsed = millis() - alert_timer;
    bool btn = button_was_pressed();

    switch (alert_phase) {

        case ALERT_WAITING:
            // Hiển thị đếm ngược log mỗi giây
            if (elapsed % 1000 < 100) {
                uint32_t remaining = (ALERT_CANCEL_WINDOW_MS - elapsed) / 1000;
                ESP_LOGW(TAG, "[ALERT] Con %lu giay de bam nut huy...", remaining);
            }

            // Bỏ qua nút trong BUTTON_IGNORE_MS đầu (tránh nhiễu lúc ngã)
            if (btn && elapsed >= BUTTON_IGNORE_MS) {
                ESP_LOGI(TAG, "[ALERT] Nguoi dung HUY trong cua so 5s.");
                alert_reset("nut bam huy (truoc khi coi)");
                break;
            } else if (btn) {
                ESP_LOGW(TAG, "[ALERT] Bo qua nut (trong %dms dau, elapsed=%lums)", BUTTON_IGNORE_MS, elapsed);
            }

            if (elapsed >= ALERT_CANCEL_WINDOW_MS) {
                // Hết 5s không bấm → bật còi
                ESP_LOGW(TAG, "[ALERT] Het 5s! BAT COI CANH BAO!");
                buzzer_on();
                alert_phase = ALERT_BUZZING;
                alert_timer = millis();   // reset timer cho BUZZING
            }
            break;

        case ALERT_BUZZING:
            // Chỉ tắt khi bấm nút – không tự reset
            if (btn) {
                ESP_LOGI(TAG, "[ALERT] Nguoi dung TAT COI.");
                alert_reset("nut bam tat coi");
            }
            break;

        default:
            break;
    }
}

//  Getters
fall_state_t fall_detector_get_state(void)
{
    return fall_state;
}

alert_phase_t fall_detector_get_alert_phase(void)
{
    return alert_phase;
}

const char *fall_state_name(fall_state_t s)
{
    static const char *names[] = {
        "NORMAL", "FREE_FALL", "WAIT_IMPACT", "POST_CHECK", "CONFIRMED"
    };
    if (s < 0 || s > STATE_FALL_CONFIRMED) return "?";
    return names[s];
}