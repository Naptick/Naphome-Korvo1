idf_component_register(SRCS "src/sensor_manager.c"
                              "src/sensor_integration.c"
                       INCLUDE_DIRS "include"
                       PRIV_REQUIRES driver esp_common esp_timer
                       REQUIRES somnus_profile esp_http_client json)
                       # TODO: Re-enable sensor driver components once component discovery is fixed
                       # REQUIRES somnus_profile esp_http_client cjson sht45 sgp40 scd40 vcnl4040 ec10)
