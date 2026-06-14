idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES bt nvs_flash esp_driver_uart esp_driver_gpio esp_wifi esp_netif esp_rom
                    INCLUDE_DIRS "." "../../../components/espnow_proto/include")

if(CONFIG_SCANNER_NRF24)
    target_sources(${COMPONENT_LIB} PRIVATE "nrf24_radio.c")
    target_link_libraries(${COMPONENT_LIB} PRIVATE idf::esp_driver_spi)
endif()
