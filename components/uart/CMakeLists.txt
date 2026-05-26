idf_component_register(
    SRCS "hlw8110.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer  # 依赖 driver 和 timer 组件
)