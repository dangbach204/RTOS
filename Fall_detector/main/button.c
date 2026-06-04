#include "button.h"
#include "config.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

static const char *TAG = "BUTTON";

// ─── Trạng thái debounce ──────────────────────────────
static bool     last_stable  = false;   // trạng thái ổn định trước đó
static bool     raw_state    = false;   // đọc thô hiện tại
static uint32_t last_change  = 0;       // thời điểm thay đổi lần cuối (ms)
static bool     pressed_flag = false;   // cờ "vừa bấm" cho button_was_pressed()

static inline uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

void button_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BUTTON_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,   // pull-down nội: LOW khi thả
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    ESP_LOGI(TAG, "Button init OK (GPIO %d, pull-down)", BUTTON_PIN);
}

/**
 * Gọi định kỳ (từ button_task) để cập nhật debounce state.
 */
static void button_poll(void)
{
    bool cur = (gpio_get_level(BUTTON_PIN) == 1);

    // Log mỗi khi level thay đổi để dễ debug
    if (cur != raw_state) {
        ESP_LOGI(TAG, "GPIO %d level: %d -> %d", BUTTON_PIN, raw_state, cur);
        raw_state   = cur;
        last_change = now_ms();
    }

    // Chỉ chuyển trạng thái ổn định sau BUTTON_DEBOUNCE_MS
    if ((now_ms() - last_change) >= BUTTON_DEBOUNCE_MS) {
        if (raw_state && !last_stable) {
            // Rising edge – nút vừa được bấm
            last_stable  = true;
            pressed_flag = true;
            ESP_LOGI(TAG, "Button pressed");
        } else if (!raw_state && last_stable) {
            // Falling edge – nút vừa được thả
            last_stable = false;
        }
    }
}

bool button_is_pressed(void)
{
    return last_stable;
}

bool button_was_pressed(void)
{
    // Poll tại chỗ để không cần task riêng
    // (alert_task gọi hàm này mỗi 100ms – đủ để debounce 50ms)
    button_poll();

    if (pressed_flag) {
        pressed_flag = false;
        ESP_LOGI(TAG, "button_was_pressed() = true");
        return true;
    }
    return false;
}