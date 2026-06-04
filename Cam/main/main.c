#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_camera.h"
#include "esp_crt_bundle.h"

// ── CONFIG ───────────────────────────────────────────────
#define WIFI_SSID      "Noobs"
#define WIFI_PASS      "brimstone"
#define BLYNK_TOKEN    "2gfpS2kFmzA3qINCBjmxkuKowynUgMap"
#define BLYNK_VPIN     "V0"

// ── CAMERA PINS (AI-THINKER ESP32-CAM) ──────────────────
#define CAM_PIN_PWDN    32
#define CAM_PIN_RESET   -1
#define CAM_PIN_XCLK     0
#define CAM_PIN_SIOD    26
#define CAM_PIN_SIOC    27
#define CAM_PIN_D7      35
#define CAM_PIN_D6      34
#define CAM_PIN_D5      39
#define CAM_PIN_D4      36
#define CAM_PIN_D3      21
#define CAM_PIN_D2      19
#define CAM_PIN_D1      18
#define CAM_PIN_D0       5
#define CAM_PIN_VSYNC   25
#define CAM_PIN_HREF    23
#define CAM_PIN_PCLK    22

// ── GLOBALS ──────────────────────────────────────────────
static const char        *TAG = "ESP32-CAM";
static httpd_handle_t     stream_httpd = NULL;
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0

// ── WIFI EVENT HANDLER ───────────────────────────────────
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// ── CAMERA INIT ──────────────────────────────────────────
static esp_err_t init_camera(void)
{
    camera_config_t config;
    memset(&config, 0, sizeof(camera_config_t));

    config.pin_pwdn     = CAM_PIN_PWDN;
    config.pin_reset    = CAM_PIN_RESET;
    config.pin_xclk     = CAM_PIN_XCLK;
    config.pin_sccb_sda = CAM_PIN_SIOD;
    config.pin_sccb_scl = CAM_PIN_SIOC;
    config.pin_d7  = CAM_PIN_D7;
    config.pin_d6  = CAM_PIN_D6;
    config.pin_d5  = CAM_PIN_D5;
    config.pin_d4  = CAM_PIN_D4;
    config.pin_d3  = CAM_PIN_D3;
    config.pin_d2  = CAM_PIN_D2;
    config.pin_d1  = CAM_PIN_D1;
    config.pin_d0  = CAM_PIN_D0;
    config.pin_vsync    = CAM_PIN_VSYNC;
    config.pin_href     = CAM_PIN_HREF;
    config.pin_pclk     = CAM_PIN_PCLK;
    config.xclk_freq_hz = 20000000;
    config.ledc_timer   = LEDC_TIMER_0;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size   = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;
    config.fb_count     = 1;
    config.fb_location  = CAMERA_FB_IN_PSRAM;
    config.grab_mode    = CAMERA_GRAB_LATEST;

    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "Camera initialized");
    return ESP_OK;
}

// ── HTTP HANDLER: /stream ────────────────────────────────
static esp_err_t stream_handler(httpd_req_t *req)
{
    camera_fb_t *fb = NULL;
    esp_err_t    res = ESP_OK;
    char         part_buf[64];

    httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");

    while (true) {
        fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Camera capture failed");
            res = ESP_FAIL;
            break;
        }

        size_t hlen = snprintf(part_buf, sizeof(part_buf),
            "--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n",
            fb->len);

        res = httpd_resp_send_chunk(req, part_buf, hlen);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
        if (res == ESP_OK)
            res = httpd_resp_send_chunk(req, "\r\n", 2);

        esp_camera_fb_return(fb);
        if (res != ESP_OK) break;
    }
    return res;
}

// ── HTTP HANDLER: / (trang web xem stream) ───────────────
static esp_err_t home_handler(httpd_req_t *req)
{
    const char *html =
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<title>ESP32-CAM Stream</title>"
        "<style>"
        "body{margin:0;background:#000;display:flex;"
        "justify-content:center;align-items:center;height:100vh}"
        "img{max-width:100%;max-height:100vh}"
        "</style>"
        "</head><body>"
        "<img src='/stream'>"
        "</body></html>";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_sendstr(req, html);
    return ESP_OK;
}

static const httpd_uri_t uri_home = {
    .uri = "/", .method = HTTP_GET, .handler = home_handler
};
static const httpd_uri_t uri_stream = {
    .uri = "/stream", .method = HTTP_GET, .handler = stream_handler
};

// ── START WEB SERVER ─────────────────────────────────────
static void start_stream_server(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.stack_size  = 8192;

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &uri_home);
        httpd_register_uri_handler(stream_httpd, &uri_stream);
        ESP_LOGI(TAG, "Stream server started on port 80");
    } else {
        ESP_LOGE(TAG, "Failed to start stream server");
    }
}

// ── BLYNK: PUSH STREAM URL LÊN VIRTUAL PIN ───────────────
//
//  Blynk Video widget đọc URL từ V0.
//  App Blynk tự kết nối đến http://<IP>/stream
//  → không cần nhập IP thủ công.
//  Yêu cầu: điện thoại và ESP32-CAM cùng WiFi.
//
static void blynk_push_stream_url(const char *ip_str)
{
    // Blynk REST API: GET /external/api/update?token=...&v0=<url>
    // URL stream cần encode: dấu ':' → %3A, '/' → %2F
    char request_url[512];
    snprintf(request_url, sizeof(request_url),
        "https://blynk.cloud/external/api/update"
        "?token=%s"
        "&%s=http%%3A%%2F%%2F%s%%2Fstream",
        BLYNK_TOKEN, BLYNK_VPIN, ip_str);

    ESP_LOGI(TAG, "Pushing to Blynk: http://%s/stream", ip_str);

    esp_http_client_config_t cfg = {
        .url               = request_url,
        .method            = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status == 200) {
            ESP_LOGI(TAG, "✓ Blynk updated! Mở app → Video widget V0");
        } else {
            ESP_LOGW(TAG, "Blynk trả về HTTP %d — kiểm tra lại token", status);
        }
    } else {
        ESP_LOGE(TAG, "✗ Blynk request thất bại: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

// ── WIFI INIT ────────────────────────────────────────────
static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid      = WIFI_SSID,
            .password  = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi connecting to %s...", WIFI_SSID);
}

// ── MAIN ─────────────────────────────────────────────────
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-CAM Stream + Blynk (ESP-IDF) ===");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Network stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Camera
    if (init_camera() != ESP_OK) {
        ESP_LOGE(TAG, "Camera init failed, halting.");
        return;
    }

    // WiFi
    wifi_init_sta();

    // Chờ có IP, tối đa 15 giây
    EventBits_t bits = xEventGroupWaitBits(
        s_wifi_event_group, WIFI_CONNECTED_BIT,
        pdFALSE, pdTRUE, pdMS_TO_TICKS(15000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "WiFi timeout, halting.");
        return;
    }

    // Lấy IP
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    esp_netif_get_ip_info(netif, &ip_info);

    char ip_str[16];
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    ESP_LOGI(TAG, "IP: %s", ip_str);

    // Khởi động stream server
    start_stream_server();
    ESP_LOGI(TAG, "Stream: http://%s/stream", ip_str);

    // Push URL lên Blynk
    blynk_push_stream_url(ip_str);

    ESP_LOGI(TAG, "=== Ready! Mở Blynk app → Video widget V0 ===");

    // Main loop
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}