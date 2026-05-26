#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>
#include "esp_err.h"

// 硬件配置 (ESP32-C3 SuperMini)
#define OLED_SCL_PIN        9
#define OLED_SDA_PIN        8
#define OLED_I2C_ADDR       0x3C    // 7位I2C地址
#define OLED_I2C_FREQ       400000  // 400kHz

// 函数声明
esp_err_t OLED_Init(void);
void OLED_Clear(void);
void OLED_SetCursor(uint8_t y, uint8_t x);
void OLED_ShowChar(uint8_t x, uint8_t y, char chr);
void OLED_ShowString(uint8_t x, uint8_t y, char *str);

#endif