#include "buzzer.h"
#include "config.h"

#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "BUZZER";

void buzzer_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BUZZER_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    gpio_set_level(BUZZER_PIN, 0);   // tắt khi khởi động
    ESP_LOGI(TAG, "Buzzer init OK (GPIO %d)", BUZZER_PIN);
}

void buzzer_on(void)
{
    ESP_LOGW(TAG, "BUZZER ON");
    gpio_set_level(BUZZER_PIN, 1);
}

void buzzer_off(void)
{
    ESP_LOGI(TAG, "Buzzer off");
    gpio_set_level(BUZZER_PIN, 0);
}
