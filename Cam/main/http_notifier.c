#include "http_notifier.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include <string.h>

static const char *TAG = "HTTP_NOTIFIER";

// HTTP client event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER: %s: %s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        if (!esp_http_client_is_chunked_response(evt->client))
        {
            // Print response
            char response[128] = {0};
            int len = evt->data_len < 127 ? evt->data_len : 127;
            memcpy(response, evt->data, len);
            ESP_LOGI(TAG, "Response from ESP32-C3: %s", response);
        }
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    case HTTP_EVENT_REDIRECT:
        ESP_LOGD(TAG, "HTTP_EVENT_REDIRECT");
        break;
    }
    return ESP_OK;
}

esp_err_t send_http_notification(const char *server_ip, int photo_count, const char *message)
{
    if (!server_ip)
    {
        ESP_LOGE(TAG, "Server IP is NULL!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "╔════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║  🚨 SENDING BUZZER TRIGGER TO ESP32-C3       ║");
    ESP_LOGI(TAG, "╚════════════════════════════════════════════════╝");
    ESP_LOGI(TAG, "Target: http://%s:8080/buzzer", server_ip);
    ESP_LOGI(TAG, "Photo count: %d", photo_count);
    if (message)
    {
        ESP_LOGI(TAG, "Message: %s", message);
    }

    // Build URL
    char url[128];
    snprintf(url, sizeof(url), "http://%s:8080/buzzer", server_ip);

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST, // ESP32-C3 accepts both GET and POST
        .event_handler = http_event_handler,
        .timeout_ms = 15000, // 15 second timeout (increased)
        .buffer_size = 512,
        .buffer_size_tx = 512,
        .keep_alive_enable = true, // Keep connection alive
        .disable_auto_redirect = false,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    // Optional: Add custom headers
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "User-Agent", "ESP32-CAM");

    // Optional: Send JSON body with notification details
    char post_data[256];
    if (message)
    {
        snprintf(post_data, sizeof(post_data),
                 "{\"source\":\"ESP32-CAM\",\"photo_count\":%d,\"message\":\"%s\"}",
                 photo_count, message);
    }
    else
    {
        snprintf(post_data, sizeof(post_data),
                 "{\"source\":\"ESP32-CAM\",\"photo_count\":%d}",
                 photo_count);
    }

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "Sending POST request...");
    ESP_LOGI(TAG, "Body: %s", post_data);

    // Perform request
    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        int content_length = esp_http_client_get_content_length(client);

        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "✅ HTTP POST SUCCESS");
        ESP_LOGI(TAG, "   Status Code: %d", status_code);
        ESP_LOGI(TAG, "   Content Length: %d", content_length);

        if (status_code == 200)
        {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "╔════════════════════════════════════════════════╗");
            ESP_LOGI(TAG, "║  🔔 BUZZER TRIGGERED SUCCESSFULLY!            ║");
            ESP_LOGI(TAG, "╚════════════════════════════════════════════════╝");
            ESP_LOGI(TAG, "");

            esp_http_client_cleanup(client);
            return ESP_OK;
        }
        else
        {
            ESP_LOGW(TAG, "Unexpected status code: %d", status_code);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }
    }
    else
    {
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "╔════════════════════════════════════════════════╗");
        ESP_LOGE(TAG, "║  ❌ HTTP REQUEST FAILED                       ║");
        ESP_LOGE(TAG, "╚════════════════════════════════════════════════╝");
        ESP_LOGE(TAG, "Error: %s", esp_err_to_name(err));
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "⚠️  POSSIBLE CAUSES:");
        ESP_LOGE(TAG, "   1. ESP32-C3 is not powered on");
        ESP_LOGE(TAG, "   2. Wrong IP address: %s", server_ip);
        ESP_LOGE(TAG, "   3. ESP32-C3 not connected to WiFi");
        ESP_LOGE(TAG, "   4. Different WiFi network");
        ESP_LOGE(TAG, "   5. Firewall blocking port 8080");
        ESP_LOGE(TAG, "");
        ESP_LOGE(TAG, "🔍 DEBUG STEPS:");
        ESP_LOGE(TAG, "   1. Check ESP32-C3 serial monitor for IP address");
        ESP_LOGE(TAG, "   2. Update ESP32C3_IP in main code");
        ESP_LOGE(TAG, "   3. Test with: curl http://%s:8080/buzzer", server_ip);
        ESP_LOGE(TAG, "");

        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
}

esp_err_t test_buzzer_connection(const char *server_ip)
{
    if (!server_ip)
    {
        ESP_LOGE(TAG, "Server IP is NULL!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "🔍 Testing connection to ESP32-C3 at %s...", server_ip);

    char url[128];
    snprintf(url, sizeof(url), "http://%s:8080/", server_ip);

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 3000,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client)
    {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);

    if (err == ESP_OK)
    {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "✅ ESP32-C3 is reachable! Status: %d", status_code);
        esp_http_client_cleanup(client);
        return ESP_OK;
    }
    else
    {
        ESP_LOGE(TAG, "❌ Cannot reach ESP32-C3: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return ESP_FAIL;
    }
}