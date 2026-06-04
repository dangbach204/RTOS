#include "blynk_client.h"
#include "config.h"

#include "esp_crt_bundle.h"
#include <stdio.h>
#include <string.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

static const char *TAG = "BLYNK";

// ═══════════════════════════════════════════════════════
//  2 queue riêng:
//  - s_queue_alert  : logEvent + virtualWrite khi té ngã → KHÔNG DROP
//  - s_queue_telem  : virtualWrite định kỳ (a_total, state) → drop được
//
//  blynk_http_task ưu tiên đọc alert trước, rồi mới đọc telemetry.
// ═══════════════════════════════════════════════════════
#define BLYNK_PATH_MAX       640
#define BLYNK_HTTP_STACK     10240
#define ALERT_QUEUE_DEPTH    10    // đủ cho nhiều lần té ngã liên tiếp
#define TELEM_QUEUE_DEPTH    4     // telemetry: drop nếu đầy, không quan trọng

typedef struct {
    char path[BLYNK_PATH_MAX];
} blynk_msg_t;

static QueueHandle_t s_queue_alert = NULL;   // ưu tiên cao
static QueueHandle_t s_queue_telem = NULL;   // ưu tiên thấp

// ─── URL-encode ───────────────────────────────────────
static void url_encode(const char *src, char *dst, size_t dst_size)
{
    static const char hex[] = "0123456789ABCDEF";
    size_t di = 0;
    for (size_t si = 0; src[si] && di + 4 < dst_size; si++) {
        unsigned char c = (unsigned char)src[si];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') ||
             c == '-' || c == '_' || c == '.' || c == '~') {
            dst[di++] = (char)c;
        } else {
            dst[di++] = '%';
            dst[di++] = hex[(c >> 4) & 0xF];
            dst[di++] = hex[c & 0xF];
        }
    }
    dst[di] = '\0';
}

// ─── HTTP GET thực sự (chỉ chạy trong blynk_http_task) ──
static void do_get_internal(const char *path)
{
    char full_url[BLYNK_PATH_MAX + 32];
    snprintf(full_url, sizeof(full_url), "https://blynk.cloud%s", path);

    esp_http_client_config_t cfg = {
        .url                         = full_url,
        .timeout_ms                  = 8000,
        .skip_cert_common_name_check = true,
        .crt_bundle_attach           = esp_crt_bundle_attach,
        .buffer_size                 = 512,
        .buffer_size_tx              = 512,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) { ESP_LOGE(TAG, "init failed"); return; }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status != 200)
            ESP_LOGW(TAG, "HTTP %d | %s", status, path);
        else
            ESP_LOGI(TAG, "HTTP 200 OK");
    } else {
        ESP_LOGE(TAG, "HTTP error: %s", esp_err_to_name(err));
    }
    esp_http_client_cleanup(client);
}

// ─── Task HTTP: ưu tiên alert queue trước ─────────────
static void blynk_http_task(void *arg)
{
    blynk_msg_t msg;
    while (1) {
        // 1. Kiểm tra alert queue trước (không chờ)
        if (xQueueReceive(s_queue_alert, &msg, 0) == pdTRUE) {
            do_get_internal(msg.path);
            continue;   // vòng lại kiểm tra alert tiếp trước khi telemetry
        }
        // 2. Không có alert → lấy telemetry (chờ tối đa 100ms)
        if (xQueueReceive(s_queue_telem, &msg, pdMS_TO_TICKS(100)) == pdTRUE) {
            do_get_internal(msg.path);
        }
    }
}

// ═══════════════════════════════════════════════════════
//  Init
// ═══════════════════════════════════════════════════════
void blynk_client_init(void)
{
    s_queue_alert = xQueueCreate(ALERT_QUEUE_DEPTH, sizeof(blynk_msg_t));
    s_queue_telem = xQueueCreate(TELEM_QUEUE_DEPTH, sizeof(blynk_msg_t));
    configASSERT(s_queue_alert);
    configASSERT(s_queue_telem);

    xTaskCreate(blynk_http_task, "blynk_http",
                BLYNK_HTTP_STACK, NULL, 3, NULL);

    ESP_LOGI(TAG, "Blynk client init OK");
}

// ═══════════════════════════════════════════════════════
//  Internal enqueue helpers
// ═══════════════════════════════════════════════════════

// Alert: block tối đa 200ms, log lỗi nếu vẫn đầy
static esp_err_t enqueue_alert(const char *path)
{
    if (!s_queue_alert) return ESP_ERR_INVALID_STATE;
    blynk_msg_t msg;
    strlcpy(msg.path, path, sizeof(msg.path));
    if (xQueueSend(s_queue_alert, &msg, pdMS_TO_TICKS(200)) != pdTRUE) {
        ESP_LOGE(TAG, "ALERT queue day! Mat su kien: %s", path);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// Telemetry: không block, drop nếu đầy (không quan trọng)
static esp_err_t enqueue_telem(const char *path)
{
    if (!s_queue_telem) return ESP_ERR_INVALID_STATE;
    blynk_msg_t msg;
    strlcpy(msg.path, path, sizeof(msg.path));
    if (xQueueSend(s_queue_telem, &msg, 0) != pdTRUE) {
        // Telemetry drop là bình thường, không log để tránh spam
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

// ═══════════════════════════════════════════════════════
//  Public API
// ═══════════════════════════════════════════════════════

// Telemetry (số liệu định kỳ) → queue thấp
esp_err_t blynk_virtual_write_float(int vpin, float value)
{
    char path[BLYNK_PATH_MAX];
    snprintf(path, sizeof(path),
             "/external/api/update?token=%s&V%d=%.4f",
             BLYNK_AUTH_TOKEN, vpin, (double)value);
    return enqueue_telem(path);
}

// virtualWrite chuỗi: phân loại theo vpin
// V0 (STATUS) và V2 (STATE) khi té ngã → alert queue
// V2 khi bình thường và V1 (a_total) → telem queue
esp_err_t blynk_virtual_write_str(int vpin, const char *value)
{
    char encoded[256];
    url_encode(value, encoded, sizeof(encoded));

    char path[BLYNK_PATH_MAX];
    snprintf(path, sizeof(path),
             "/external/api/update?token=%s&V%d=%s",
             BLYNK_AUTH_TOKEN, vpin, encoded);

    // V0 (STATUS): luôn alert nếu là cảnh báo té ngã
    if (vpin == VPIN_STATUS &&
        (strstr(value, "NGA") || strstr(value, "CONFIRMED")))
        return enqueue_alert(path);

    return enqueue_telem(path);
}

// logEvent: luôn dùng alert queue → KHÔNG BAO GIỜ DROP
esp_err_t blynk_log_event(const char *event_name, const char *message)
{
    char enc_msg[256];
    url_encode(message, enc_msg, sizeof(enc_msg));

    char path[BLYNK_PATH_MAX];
    snprintf(path, sizeof(path),
             "/external/api/logEvent?token=%s&code=%s&description=%s",
             BLYNK_AUTH_TOKEN, event_name, enc_msg);
    return enqueue_alert(path);
}