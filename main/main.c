#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_log.h"

// 所有驱动头文件
#include "oled.h"
#include "hlw8110.h"
#include "button.h"
#include "connect.h"

#define RELAY_PIN 1
#define BUZZER_PIN 0 // 蜂鸣器引脚，请根据实际硬件修改
#define TAG "MAIN_APP"

// ==================== 全局状态 ====================
static bool g_relay_state = true;
float power_threshold = 2110.0f; 
static bool g_is_overload = false; // [新增] 过载锁死标志位

// ==================== 继电器接口 ====================
void Relay_Set_State(bool turn_on) {
    if (g_relay_state != turn_on) {
        g_relay_state = turn_on;
        gpio_set_level(RELAY_PIN, turn_on ? 0 : 1);
        ESP_LOGI(TAG, "继电器动作: %s", turn_on ? "ON" : "OFF");
        
        // 继电器状态变化立即上报
        if (g_current_mode == MODE_WIFI_ONLINE && g_wifi_status == WIFI_STATUS_CONNECTED) {
            extern bool g_need_immediate_post; 
            g_need_immediate_post = true;
        }
    }
}

// ==================== 蜂鸣器接口 ====================
void Buzzer_Set_State(bool turn_on) {
    // 假设高电平响，低电平不响。若是低电平触发，请把 1 和 0 互换
    gpio_set_level(BUZZER_PIN, turn_on ? 1 : 0);
}

bool Relay_Get_State(void) {
    return g_relay_state;
}

// 立即上报标志位
bool g_need_immediate_post = false;

// ==================== 主函数 ====================
void app_main(void) {
    // 1. 系统基础初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. 硬件初始化
    ESP_LOGI(TAG, "=== 最终版启动（支持过载锁死报警） ===");
    OLED_Init();
    HLW8110_Init();
    Button_Init();
    
    // 初始化继电器
    gpio_reset_pin(RELAY_PIN);
    gpio_set_direction(RELAY_PIN, GPIO_MODE_OUTPUT);
    Relay_Set_State(true);

    // 初始化蜂鸣器
    gpio_reset_pin(BUZZER_PIN);
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
    Buzzer_Set_State(false);

    // 3. 网络初始化
    ESP_LOGI(TAG, "默认进入 WiFi 在线模式");
    Wifi_Init_Station();

    // 变量定义
    hlw8110_data_t power_data = {0}; 
    char line_buf[32];
    uint8_t disp_timer = 0;
    uint16_t mqtt_post_counter = 0;
    const uint16_t POST_INTERVAL = 120; // 6秒上报一次
    static uint8_t wifi_key_press_count = 0;

    float last_voltage = 0.0f;
    float last_current = 0.0f;
    float last_power = 0.0f;
    float last_pf = 0.0f;

    OLED_Clear();

    while (1) {
        // --- A. 按键处理 ---
        key_event_t key = Button_Scan();
        
        if (key != KEY_NONE) {
            // [新增] 只要按下任何按键，就解除报警锁定状态
            if (g_is_overload) {
                g_is_overload = false;
                Buzzer_Set_State(false); // 停止蜂鸣器
                ESP_LOGI(TAG, "用户按键，解除过载报警状态");
                // 注意：这里仅解除报警，不自动闭合继电器，需排查故障后通过网络重新闭合
            } else {
                // 如果没有过载，正常处理按键逻辑
                if (key == KEY_UP) {
                    power_threshold += 10.0f;
                    if (power_threshold > 3500) power_threshold = 3500;
                }
                if (key == KEY_DOWN) {
                    power_threshold -= 10.0f;
                    if (power_threshold < 0) power_threshold = 0;
                }
                if (key == KEY_WIFI) {
                    wifi_key_press_count++;
                    if (wifi_key_press_count == 1) {
                        Mode_Switch_Now();
                    }
                }
            }
        } 
        
        if (key != KEY_WIFI) {
            wifi_key_press_count = 0;
        }

        // --- B. 核心逻辑（每500ms执行一次） ---
        if (++disp_timer >= 10) { 
            disp_timer = 0;
            
            esp_err_t err = HLW8110_ReadAll(&power_data);

            char *mode_str = (g_current_mode == MODE_WIFI_ONLINE) ? "WIFI" : "OFFL";
            snprintf(line_buf, sizeof(line_buf), "T:%4.0fW  %s", power_threshold, mode_str);
            OLED_ShowString(0, 0, line_buf);

            if (err == ESP_OK) {
                last_voltage = power_data.voltage_V;
                last_current = power_data.current_A;
                last_power = power_data.power_W;
                last_pf = power_data.pf;

                snprintf(line_buf, sizeof(line_buf), "V:%5.1f I:%5.3f ", last_voltage, last_current);
                OLED_ShowString(0, 2, line_buf);
                snprintf(line_buf, sizeof(line_buf), "P:%5.1fW PF:%4.2f ", last_power, last_pf);
                OLED_ShowString(0, 4, line_buf);

                // [修改] 本地过载保护 + 锁死报警逻辑
                if (last_power > power_threshold && power_threshold > 10.0f) {
                    if (!g_is_overload) { // 第一次检测到过载
                        g_is_overload = true;       // 触发锁死标志
                        Relay_Set_State(false);     // 1. 马上切断继电器
                        Buzzer_Set_State(true);     // 2. 蜂鸣器开始长鸣
                        ESP_LOGW(TAG, "检测到过载，启动锁死报警！");
                    }
                } 
                
                // 界面显示处理：根据锁死标志位显示状态
                if (g_is_overload) {
                    OLED_ShowString(0, 6, "!! OVERLOAD !!  ");
                } else {
                    OLED_ShowString(0, 6, g_relay_state ? "Status: Normal  " : "Status: RLY OFF ");
                }
            } else {
                OLED_ShowString(0, 2, "Sensor Error!   ");
                snprintf(line_buf, sizeof(line_buf), "P:%5.1fW        ", last_power);
                OLED_ShowString(0, 4, line_buf);
                
                // 传感器故障时，也要维持锁死报警的屏幕提示
                if (g_is_overload) {
                    OLED_ShowString(0, 6, "!! OVERLOAD !!  ");
                } else {
                    OLED_ShowString(0, 6, g_relay_state ? "Status: Normal  " : "Status: RLY OFF ");
                }
            }
        }

        // --- C. MQTT上报逻辑 ---
        if (g_current_mode == MODE_WIFI_ONLINE && g_wifi_status == WIFI_STATUS_CONNECTED) {
            if (g_need_immediate_post) {
                g_need_immediate_post = false;
                MqttApp_Post_Property(g_relay_state, last_voltage, last_current, last_power, last_pf);
                mqtt_post_counter = 0;
            } else if (++mqtt_post_counter >= POST_INTERVAL) {
                mqtt_post_counter = 0;
                MqttApp_Post_Property(g_relay_state, last_voltage, last_current, last_power, last_pf);
            }
        } else {
            mqtt_post_counter = 0;
            g_need_immediate_post = false;
        }

        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}