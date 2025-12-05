# Only build somnus_ble if Bluetooth is enabled
if(CONFIG_BT_ENABLED OR CONFIG_BT_NIMBLE_ENABLED OR CONFIG_BT_BLUEDROID_ENABLED)
    idf_component_register(SRCS "src/somnus_ble.c"
                           INCLUDE_DIRS "include"
                           REQUIRES somnus_profile esp_wifi bt nvs_flash json)
else()
    # Create a stub component when Bluetooth is disabled
    idf_component_register(INCLUDE_DIRS "include")
endif()
