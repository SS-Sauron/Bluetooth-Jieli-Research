idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio esp_wifi esp_netif
                    INCLUDE_DIRS "." "../../esp32_avrcp_console/main")
