#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "config.h"
#include "mpu6050.h"
#include "fall_detector.h"
#include "blynk_client.h"
#include "button.h"

static const char *TAG = "MAIN";

//  WiFi
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define WIFI_MAX_RETRY     10

static EventGroupHandle_t s_wifi_event_group;
static int                s_retry_num = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGW(TAG, "Ket noi lai WiFi... lan %d", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&e->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t inst_any, inst_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &inst_any));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &inst_got_ip));

    wifi_config_t wifi_cfg = {
        .sta = {
            .ssid     = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_connect());

    ESP_LOGI(TAG, "Dang ket noi WiFi...");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(30000));
    if (bits & WIFI_CONNECTED_BIT)
        ESP_LOGI(TAG, "WiFi ket noi OK!");
    else
        ESP_LOGE(TAG, "WiFi THAT BAI!");
}

//  Biến chia sẻ giữa các task
static volatile float g_current_a_total = 1.0f;

//  Task gửi Blynk định kỳ 500ms
//  Chỉ gọi blynk_virtual_write_* → enqueue, KHÔNG làm HTTP trực tiếp
//  → Stack nhỏ là đủ
static void blynk_send_task(void *arg)
{
    static const char *state_str[] = {
        "Normal", "Free Fall", "Wait Impact", "Post Check", "CONFIRMED"
    };

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(BLYNK_SEND_MS));

        fall_state_t st = fall_detector_get_state();

        blynk_virtual_write_float(VPIN_A_TOTAL, g_current_a_total);
        blynk_virtual_write_str(VPIN_STATE, state_str[(int)st]);

        if (st == STATE_NORMAL)
            blynk_virtual_write_str(VPIN_STATUS, "OK - Binh thuong");
    }
}

//  Task IMU 50Hz
static void imu_task(void *arg)
{
    static uint32_t last_print = 0;

    while (1) {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000ULL);

        imu_data_t d = mpu_read();
        g_current_a_total = d.a_total;

        fall_detector_update(&d);

        if (now - last_print >= SERIAL_PRINT_MS) {
            last_print = now;
            ESP_LOGI(TAG, "a=%6.3fg  g=%6.1f deg/s  [%s]",
                     d.a_total, d.g_total,
                     fall_state_name(fall_detector_get_state()));
        }

        vTaskDelay(pdMS_TO_TICKS(IMU_LOOP_MS));
    }
}

//  Task alert – poll nút + tick còi 100ms
static void alert_task(void *arg)
{
    while (1) {
        fall_detector_alert_tick();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//  app_main
void app_main(void)
{
    ESP_LOGI(TAG, "\n=== FALL DETECTION + BLYNK (ESP-IDF) ===");

    // 1. NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. WiFi
    wifi_init_sta();

    // 3. Khởi tạo Blynk HTTP queue + task (stack 10KB riêng cho TLS)
    //    GỌI SAU KHI có WiFi để task sẵn sàng ngay
    blynk_client_init();

    // 4. I2C + MPU
    ESP_ERROR_CHECK(mpu_init());

    // 5. Hiệu chỉnh
    mpu_calibrate();

    // 6. Máy trạng thái
    fall_detector_init();

    // 7. Trạng thái ban đầu Blynk (chỉ enqueue, không block)
    blynk_virtual_write_str(VPIN_STATUS, "OK - Binh thuong");
    blynk_virtual_write_str(VPIN_STATE,  "Normal");

    ESP_LOGI(TAG, "San sang!\n");

    // 8. Tạo các FreeRTOS task
    //    blynk_http_task đã được tạo bên trong blynk_client_init()
    xTaskCreate(imu_task,        "imu_task",    IMU_TASK_STACK,
                NULL, IMU_TASK_PRIORITY,    NULL);
    xTaskCreate(blynk_send_task, "blynk_send",  BLYNK_TASK_STACK,
                NULL, BLYNK_TASK_PRIORITY,  NULL);
    xTaskCreate(alert_task,      "alert_task",  BUTTON_TASK_STACK,
                NULL, BUTTON_TASK_PRIORITY, NULL);
}