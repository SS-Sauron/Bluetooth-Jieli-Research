idf_component_register(SRCS "main.c" "menu.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES bt nvs_flash esp_system console vfs esp_driver_uart esp_driver_gpio esp_wifi esp_netif esp_event)
