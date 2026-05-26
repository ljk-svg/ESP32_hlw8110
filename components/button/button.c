#include "button.h"
#include "driver/gpio.h"

// 对应你的硬件引脚
#define PIN_WIFI   5
#define PIN_UP     6
#define PIN_DOWN   7

void Button_Init(void) {
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_WIFI) | (1ULL << PIN_UP) | (1ULL << PIN_DOWN),
        .pull_down_en = 0,
        .pull_up_en = 1, // 内部上拉，按下为低电平0
    };
    gpio_config(&io_conf);
}

key_event_t Button_Scan(void) {
    if (gpio_get_level(PIN_WIFI) == 0) return KEY_WIFI;
    if (gpio_get_level(PIN_UP) == 0)   return KEY_UP;
    if (gpio_get_level(PIN_DOWN) == 0) return KEY_DOWN;
    return KEY_NONE;
}