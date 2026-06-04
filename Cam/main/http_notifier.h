/**
 * @file http_notifier.h
 * @brief HTTP Notification module for sending triggers to ESP32-C3 buzzer server
 *
 * This module handles HTTP POST/GET requests to notify the ESP32-C3 buzzer server
 * when a person is detected and email is sent successfully.
 */

#ifndef HTTP_NOTIFIER_H
#define HTTP_NOTIFIER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Send HTTP notification to ESP32-C3 buzzer server
     *
     * This function sends an HTTP POST request to the ESP32-C3 to trigger the buzzer.
     * It uses the /buzzer endpoint which is compatible with the ESP32-C3 server.
     *
     * @param server_ip IP address of ESP32-C3 server (e.g., "192.168.1.100")
     * @param photo_count Current photo counter for logging
     * @param message Notification message to send (optional, can be NULL)
     *
     * @return
     *     - ESP_OK on success
     *     - ESP_FAIL on failure
     */
    esp_err_t send_http_notification(const char *server_ip, int photo_count, const char *message);

    /**
     * @brief Test connection to ESP32-C3 buzzer server
     *
     * This function sends a simple GET request to test if the ESP32-C3 is reachable.
     *
     * @param server_ip IP address of ESP32-C3 server
     *
     * @return
     *     - ESP_OK if server is reachable
     *     - ESP_FAIL if server is not reachable
     */
    esp_err_t test_buzzer_connection(const char *server_ip);

#ifdef __cplusplus
}
#endif

#endif // HTTP_NOTIFIER_H