idf_component_register(
    SRCS "main.c" "aegis_config.c" "wifi_manager.c" "net_client.c" "medicine_source.c" "sensors.c"
    INCLUDE_DIRS "."
    REQUIRES nvs_flash esp_wifi esp_event esp_netif esp_http_client driver esp_timer esp_system lwip aegis_frame)
