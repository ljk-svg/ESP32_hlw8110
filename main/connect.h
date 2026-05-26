#ifndef __CONNECT_H__
#define __CONNECT_H__

#include <stdbool.h>
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"

// WiFi 状态枚举
typedef enum {
    WIFI_STATUS_OFFLINE,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED
} wifi_status_t;

// ==================== 工作模式枚举 ====================
typedef enum {
    MODE_WIFI_ONLINE,   // WiFi在线模式（连OneNET）
    MODE_LOCAL_OFFLINE   // 离线模式（仅本地控制）
} app_work_mode_t;

// 全局状态声明
extern wifi_status_t g_wifi_status;
extern app_work_mode_t g_current_mode; // 当前工作模式

// ==================== WiFi 热点配置 ====================
#define STATION_SSID      "jk" 
#define STATION_PASSWORD  "12345678"

// ==================== OneNET 配置（100%匹配你的控制台） ====================
#define ONENET_PRODUCT_ID   "19Lw4560y1"
#define ONENET_DEVICE_ID    "esp32c3"
#define ONENET_TOKEN        "version=2018-10-31&res=products%2F19Lw4560y1%2Fdevices%2Fesp32c3&et=1805545509&method=md5&sign=%2B2kW%2FzXwmWbF1G7r5CzTkA%3D%3D"

// 服务器地址&端口
#define ONENET_HOST         "mqtts.heclouds.com"
#define ONENET_PORT         1883

// ==================== Topic 定义 ====================
#define ONENET_TOPIC_PREFIX     "$sys/" ONENET_PRODUCT_ID "/" ONENET_DEVICE_ID
#define ONENET_SUB_SET_TOPIC    ONENET_TOPIC_PREFIX "/thing/property/set"
#define ONENET_SUB_REPLY_TOPIC  ONENET_TOPIC_PREFIX "/thing/property/post/reply"
#define ONENET_PUB_POST_TOPIC   ONENET_TOPIC_PREFIX "/thing/property/post"
#define ONENET_PUB_REPLY_TOPIC  ONENET_TOPIC_PREFIX "/thing/property/set_reply"

// ==================== 物模型标识符 ====================
#define PROP_RELAY      "switch_relay"
#define PROP_VOLTAGE    "voltage"
#define PROP_CURRENT    "current"
#define PROP_POWER      "power"
#define PROP_PF         "power_factor"  // <--- 重点：加上这一行！
// ==================== 函数声明 ====================
void Wifi_Init_Station(void);
void MqttApp_Start(void);
void MqttApp_Post_Property(bool relay_state, float voltage, float current, float power, float pf);
// ...
void Mode_Switch_Now(void); // 新增：一键切换模式

extern void Relay_Set_State(bool turn_on); 
extern bool Relay_Get_State(void);

#endif