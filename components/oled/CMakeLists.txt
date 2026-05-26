idf_component_register(
    SRCS "oled.c"
    INCLUDE_DIRS "."
    REQUIRES driver  # 重要：因为你的oled.c里用了 driver/i2c_master.h
)