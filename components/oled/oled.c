#include <string.h>
#include "oled.h"
#include "oled_font.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "OLED";

static i2c_master_bus_handle_t bus_handle;
static i2c_master_dev_handle_t dev_handle;

// 发送命令
static esp_err_t OLED_WriteCmd(uint8_t command)
{
    // Co=0, D/C#=0 => 0x00
    uint8_t buffer[2] = {0x00, command};
    return i2c_master_transmit(dev_handle, buffer, sizeof(buffer), -1);
}

// 发送数据
static esp_err_t OLED_WriteData(uint8_t data)
{
    // Co=0, D/C#=1 => 0x40
    uint8_t buffer[2] = {0x40, data};
    return i2c_master_transmit(dev_handle, buffer, sizeof(buffer), -1);
}

// 设置光标 (Y:页 0-7, X:列 0-127)
void OLED_SetCursor(uint8_t y, uint8_t x)
{
    OLED_WriteCmd(0xB0 | y);                 // 设置页地址
    OLED_WriteCmd(0x10 | ((x & 0xF0) >> 4)); // 列地址高4位
    OLED_WriteCmd(0x00 | (x & 0x0F));        // 列地址低4位
}

// 清屏
void OLED_Clear(void)
{
    for (uint8_t i = 0; i < 8; i++) {
        OLED_WriteCmd(0xB0 + i);
        OLED_WriteCmd(0x00);
        OLED_WriteCmd(0x10);
        for (uint8_t n = 0; n < 128; n++) {
            OLED_WriteData(0x00);
        }
    }
}

// 显示字符 (使用 8x16 字库)
void OLED_ShowChar(uint8_t x, uint8_t y, char chr)
{
    uint8_t c = chr - ' '; // 计算偏移量，因为字库从空格开始
    if (c >= sizeof(OLED_F8x16)/sizeof(OLED_F8x16[0])) {
        c = 0; // 超出范围默认显示空格
    }

    if (x > 120) { x = 0; y += 2; }
    
    // 画上半部分
    OLED_SetCursor(y, x);
    for (uint8_t i = 0; i < 8; i++) {
        OLED_WriteData(OLED_F8x16[c][i]); 
    }
    
    // 画下半部分
    OLED_SetCursor(y + 1, x);
    for (uint8_t i = 0; i < 8; i++) {
        OLED_WriteData(OLED_F8x16[c][i + 8]);
    }
}

// 显示字符串
void OLED_ShowString(uint8_t x, uint8_t y, char *str)
{
    while (*str) {
        if (x > 120) { x = 0; y += 2; } // 自动换行
        OLED_ShowChar(x, y, *str);
        x += 8;
        str++;
    }
}

// 初始化
esp_err_t OLED_Init(void)
{
    // 1. 初始化 I2C 总线
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = OLED_SDA_PIN,
        .scl_io_num = OLED_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // C3 SuperMini 必须开内部上拉
    };
    
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    // 2. 添加设备
    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = OLED_I2C_FREQ,
    };

    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &dev_handle));

    ESP_LOGI(TAG, "OLED I2C Initialized");

    // 3. SSD1306 初始化序列
    vTaskDelay(pdMS_TO_TICKS(100));
    OLED_WriteCmd(0xAE); // Display Off
    OLED_WriteCmd(0x20); // Set Memory Addressing Mode
    OLED_WriteCmd(0x10); // 00=Horz, 01=Vert, 10=Page
    OLED_WriteCmd(0xB0); // Set Page Start
    OLED_WriteCmd(0xC8); // Set COM Output Scan Direction
    OLED_WriteCmd(0x00); // Set Low Column
    OLED_WriteCmd(0x10); // Set High Column
    OLED_WriteCmd(0x40); // Set Start Line
    OLED_WriteCmd(0x81); // Contrast Control
    OLED_WriteCmd(0xFF); 
    OLED_WriteCmd(0xA1); // Segment Re-map
    OLED_WriteCmd(0xA6); // Normal Display
    OLED_WriteCmd(0xA8); // Multiplex Ratio
    OLED_WriteCmd(0x3F); 
    OLED_WriteCmd(0xA4); // Output Follows RAM
    OLED_WriteCmd(0xD3); // Display Offset
    OLED_WriteCmd(0x00); 
    OLED_WriteCmd(0xD5); // Clock Divide Ratio
    OLED_WriteCmd(0xF0); 
    OLED_WriteCmd(0xD9); // Pre-charge Period
    OLED_WriteCmd(0x22); 
    OLED_WriteCmd(0xDA); // COM Pins Hardware Config
    OLED_WriteCmd(0x12); 
    OLED_WriteCmd(0xDB); // VCOMH Deselect Level
    OLED_WriteCmd(0x20); 
    OLED_WriteCmd(0x8D); // Charge Pump
    OLED_WriteCmd(0x14); 
    OLED_WriteCmd(0xAF); // Display On

    OLED_Clear();
    return ESP_OK;
}