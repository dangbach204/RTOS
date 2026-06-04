idf_component_register(
    SRCS "http_notifier.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES 
        esp_http_server 
        esp_http_client
        json
        espressif__esp32-camera 
        esp-tls 
        mbedtls 
        esp_wifi
        nvs_flash
        driver
        esp_netif
        esp_event
        lwip
        esp_timer
        espressif__esp_rainmaker
)