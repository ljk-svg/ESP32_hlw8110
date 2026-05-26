idf_component_register(
    SRCS "main.c" "connect.c"
    INCLUDE_DIRS "."
    # 删掉了 bt，保留核心硬件驱动和 Wi-Fi 必须的组件
    REQUIRES nvs_flash esp_wifi esp_event driver oled uart button mqtt json  mbedtls
)