// // ESP32-C3 RainMaker Bridge
// // Nhận thông báo từ ESP32-CAM qua ESP-NOW → Gửi lên RainMaker

// #include <string.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "esp_system.h"
// #include "esp_wifi.h"
// #include "esp_event.h"
// #include "esp_log.h"
// #include "nvs_flash.h"
// #include "esp_netif.h"
// #include "esp_now.h"
// #include "esp_mac.h"
// #include <esp_rmaker_core.h>
// #include <esp_rmaker_standard_params.h>
// #include <esp_rmaker_standard_devices.h>
// #include <esp_rmaker_ota.h>
// #include <esp_rmaker_schedule.h>
// #include <wifi_provisioning/manager.h>
// #include <wifi_provisioning/scheme_ble.h>
// #include <qrcode.h>

// static const char *TAG = "ESP32C3-BRIDGE";

// // ESP-NOW message structure (phải giống ESP32-CAM)
// typedef struct
// {
//     uint8_t msg_type; // 1=email_sent, 2=person_detected
//     uint32_t photo_count;
//     char message[100];
// } espnow_message_t;

// // RainMaker
// static esp_rmaker_device_t *notification_device = NULL;
// static esp_rmaker_param_t *notification_param = NULL;
// static esp_rmaker_param_t *counter_param = NULL;

// // ===== ESP-NOW CALLBACK =====
// static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len)
// {
//     if (len == sizeof(espnow_message_t))
//     {
//         espnow_message_t *msg = (espnow_message_t *)data;

//         ESP_LOGI(TAG, "📩 Received from ESP32-CAM:");
//         ESP_LOGI(TAG, "  Type: %d", msg->msg_type);
//         ESP_LOGI(TAG, "  Count: %d", msg->photo_count);
//         ESP_LOGI(TAG, "  Message: %s", msg->message);

//         // Gửi lên RainMaker
//         if (notification_param && notification_device)
//         {
//             // Update notification text
//             esp_rmaker_param_update_and_report(notification_param,
//                                                esp_rmaker_str(msg->message));

//             // Update counter
//             if (counter_param)
//             {
//                 esp_rmaker_param_update_and_report(counter_param,
//                                                    esp_rmaker_int(msg->photo_count));
//             }

//             // Raise alert
//             esp_rmaker_raise_alert("Person Detected!");

//             ESP_LOGI(TAG, "✓ Sent to RainMaker app");
//         }
//     }
// }

// // ===== ESP-NOW INIT =====
// static void espnow_init(void)
// {
//     ESP_LOGI(TAG, "Initializing ESP-NOW...");

//     esp_err_t ret = esp_now_init();
//     if (ret != ESP_OK)
//     {
//         ESP_LOGE(TAG, "ESP-NOW init failed");
//         return;
//     }

//     esp_now_register_recv_cb(espnow_recv_cb);

//     // Print MAC address
//     uint8_t mac[6];
//     esp_read_mac(mac, ESP_MAC_WIFI_STA);
//     ESP_LOGI(TAG, "");
//     ESP_LOGI(TAG, "======================================");
//     ESP_LOGI(TAG, "ESP32-C3 MAC Address:");
//     ESP_LOGI(TAG, "{0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X}",
//              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
//     ESP_LOGI(TAG, "");
//     ESP_LOGI(TAG, "Copy địa chỉ này vào ESP32-CAM code:");
//     ESP_LOGI(TAG, "esp32c3_mac[] = {0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X};",
//              mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
//     ESP_LOGI(TAG, "======================================");
//     ESP_LOGI(TAG, "");

//     ESP_LOGI(TAG, "ESP-NOW initialized");
// }

// // ===== WIFI EVENT HANDLER =====
// static void event_handler(void *arg, esp_event_base_t event_base,
//                           int32_t event_id, void *event_data)
// {
//     if (event_base == WIFI_PROV_EVENT)
//     {
//         switch (event_id)
//         {
//         case WIFI_PROV_START:
//             ESP_LOGI(TAG, "Provisioning started");
//             break;
//         case WIFI_PROV_CRED_RECV:
//             ESP_LOGI(TAG, "Received WiFi credentials");
//             break;
//         case WIFI_PROV_CRED_SUCCESS:
//             ESP_LOGI(TAG, "Provisioning successful");
//             break;
//         case WIFI_PROV_END:
//             wifi_prov_mgr_deinit();
//             break;
//         }
//     }
//     else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
//     {
//         esp_wifi_connect();
//     }
//     else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
//     {
//         ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
//         ESP_LOGI(TAG, "Connected with IP: " IPSTR, IP2STR(&event->ip_info.ip));
//     }
// }

// // ===== RAINMAKER INIT =====
// static void rainmaker_init(void)
// {
//     ESP_LOGI(TAG, "Initializing RainMaker...");

//     esp_rmaker_config_t rainmaker_cfg = {
//         .enable_time_sync = true,
//     };

//     esp_rmaker_node_t *node = esp_rmaker_node_init(&rainmaker_cfg, "ESP32C3-Bridge", "Gateway");
//     if (!node)
//     {
//         ESP_LOGE(TAG, "RainMaker node init failed");
//         return;
//     }

//     // Create notification device
//     notification_device = esp_rmaker_device_create("Camera Notifications", NULL, NULL);
//     esp_rmaker_device_add_cb(notification_device, NULL, NULL);

//     // Add notification parameter
//     notification_param = esp_rmaker_param_create("Notification", ESP_RMAKER_PARAM_NAME,
//                                                  esp_rmaker_str("Waiting for ESP32-CAM..."),
//                                                  PROP_FLAG_READ);
//     esp_rmaker_device_add_param(notification_device, notification_param);

//     // Add counter parameter
//     counter_param = esp_rmaker_param_create("Photos Sent", "esp.param.photos",
//                                             esp_rmaker_int(0), PROP_FLAG_READ);
//     esp_rmaker_device_add_param(notification_device, counter_param);

//     esp_rmaker_node_add_device(node, notification_device);

//     // Enable services
//     esp_rmaker_ota_enable_default();
//     esp_rmaker_timezone_service_enable();
//     esp_rmaker_schedule_enable();
//     esp_rmaker_system_service_enable();

//     // Start RainMaker
//     esp_rmaker_start();

//     ESP_LOGI(TAG, "RainMaker started");
// }

// // ===== WIFI PROVISIONING =====
// static void start_provisioning(void)
// {
//     ESP_LOGI(TAG, "Starting WiFi provisioning...");

//     wifi_prov_mgr_config_t config = {
//         .scheme = wifi_prov_scheme_ble,
//         .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM};

//     ESP_ERROR_CHECK(wifi_prov_mgr_init(config));

//     bool provisioned = false;
//     ESP_ERROR_CHECK(wifi_prov_mgr_is_provisioned(&provisioned));

//     if (!provisioned)
//     {
//         ESP_LOGI(TAG, "Starting provisioning via BLE");

//         const char *service_name = "PROV_ESP32C3";
//         const char *service_key = NULL;

//         wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
//         const char *pop = "abcd1234";

//         ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(security, pop, service_name, service_key));

//         // Print QR code
//         wifi_prov_print_qr(service_name, "rainmaker_esp", "ble");
//     }
//     else
//     {
//         ESP_LOGI(TAG, "Already provisioned, starting WiFi");
//         wifi_prov_mgr_deinit();
//         esp_wifi_start();
//     }
// }

// // ===== MAIN =====
// void app_main(void)
// {
//     ESP_LOGI(TAG, "");
//     ESP_LOGI(TAG, "========================================");
//     ESP_LOGI(TAG, "ESP32-C3 RainMaker Bridge");
//     ESP_LOGI(TAG, "========================================");

//     // Initialize NVS
//     esp_err_t ret = nvs_flash_init();
//     if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
//     {
//         ESP_ERROR_CHECK(nvs_flash_erase());
//         ret = nvs_flash_init();
//     }
//     ESP_ERROR_CHECK(ret);

//     // Initialize networking
//     ESP_ERROR_CHECK(esp_netif_init());
//     ESP_ERROR_CHECK(esp_event_loop_create_default());
//     esp_netif_create_default_wifi_sta();

//     // WiFi init
//     wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
//     ESP_ERROR_CHECK(esp_wifi_init(&cfg));
//     ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

//     // Register event handlers
//     ESP_ERROR_CHECK(esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
//     ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
//     ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL));

//     // Initialize RainMaker
//     rainmaker_init();

//     // Start WiFi provisioning
//     start_provisioning();

//     // Wait for WiFi
//     vTaskDelay(pdMS_TO_TICKS(10000));

//     // Initialize ESP-NOW (after WiFi started)
//     espnow_init();

//     ESP_LOGI(TAG, "");
//     ESP_LOGI(TAG, "========================================");
//     ESP_LOGI(TAG, "System Ready!");
//     ESP_LOGI(TAG, "1. Scan QR code with ESP RainMaker app");
//     ESP_LOGI(TAG, "2. Copy MAC address to ESP32-CAM code");
//     ESP_LOGI(TAG, "3. Enjoy push notifications!");
//     ESP_LOGI(TAG, "========================================");

//     while (1)
//     {
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }
