#include <string.h>
#include <stdlib.h>
#include "connect.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include <stdio.h> // 用于 sprintf

static const char *TAG = "WIFI_SYS";
wifi_status_t g_wifi_status = WIFI_STATUS_OFFLINE;
app_work_mode_t g_current_mode = MODE_WIFI_ONLINE;

static bool s_is_initialized = false;
static bool s_wifi_started = false;
static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_connected = false;
/**
 * @brief 向 OneNET 平台回复指令执行结果
 * 必须回复，否则平台会认为设备超时，导致后续指令下发失败
 */
static void send_set_reply(esp_mqtt_client_handle_t client, const char* id) {
    cJSON *reply_root = cJSON_CreateObject();
    if (reply_root == NULL) return;

    cJSON_AddStringToObject(reply_root, "id", id);
    cJSON_AddNumberToObject(reply_root, "code", 200);
    cJSON_AddStringToObject(reply_root, "msg", "success");

    char *reply_payload = cJSON_PrintUnformatted(reply_root);
    if (reply_payload) {
        ESP_LOGI(TAG, ">>> 回复平台指令 [%s]: %s", id, reply_payload);
        int pub_ret = esp_mqtt_client_publish(client, ONENET_PUB_REPLY_TOPIC, reply_payload, 0, 0, 0);
        if (pub_ret == -1) {
            ESP_LOGE(TAG, "❌ 回复平台失败！");
        }
        free(reply_payload);
    }

    cJSON_Delete(reply_root);
}
void Mode_Switch_Now(void) {
    if (g_current_mode == MODE_WIFI_ONLINE) {
        ESP_LOGI(TAG, ">>> 切换到离线模式 <<<");
        if (s_mqtt_client != NULL) {
            esp_mqtt_client_stop(s_mqtt_client);
            esp_mqtt_client_destroy(s_mqtt_client);
            s_mqtt_client = NULL;
            s_mqtt_connected = false;
        }
        if (s_wifi_started) {
            esp_wifi_stop();
            s_wifi_started = false;
        }
        g_wifi_status = WIFI_STATUS_OFFLINE;
        g_current_mode = MODE_LOCAL_OFFLINE;
    } else {
        ESP_LOGI(TAG, ">>> 切换到WiFi在线模式 <<<");
        g_current_mode = MODE_WIFI_ONLINE;
        g_wifi_status = WIFI_STATUS_CONNECTING;
        if (!s_is_initialized) {
            Wifi_Init_Station();
        } else {
            esp_wifi_start();
            s_wifi_started = true;
        }
    }
}

static void esp_event_cb(void* arg, esp_event_base_t base, int32_t id, void* data) {
    if (g_current_mode == MODE_LOCAL_OFFLINE) return;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        g_wifi_status = WIFI_STATUS_CONNECTING;
        esp_wifi_connect();
    } 
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (g_current_mode == MODE_WIFI_ONLINE) {
            g_wifi_status = WIFI_STATUS_CONNECTING;
            esp_wifi_connect(); 
        }
    } 
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        g_wifi_status = WIFI_STATUS_CONNECTED;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) data;
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "热点连接成功! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "========================================");
        vTaskDelay(pdMS_TO_TICKS(1000));
        MqttApp_Start();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_mqtt_connected = true;
            ESP_LOGI(TAG, "✅ OneNET 连接成功！");
            // 打印订阅的Topic，确认订阅成功
            ESP_LOGI(TAG, "订阅控制Topic: %s", ONENET_SUB_SET_TOPIC);
            int sub1_ret = esp_mqtt_client_subscribe(event->client, ONENET_SUB_SET_TOPIC, 0);
            int sub2_ret = esp_mqtt_client_subscribe(event->client, ONENET_SUB_REPLY_TOPIC, 0);
            ESP_LOGI(TAG, "订阅结果 | 控制Topic: %d | 回复Topic: %d", sub1_ret, sub2_ret);
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, ">>> 收到平台下发数据！");
            ESP_LOGI(TAG, "Topic: %.*s", event->topic_len, event->topic);
            ESP_LOGI(TAG, "完整内容: %.*s", event->data_len, event->data);

            // 处理平台下发的控制指令
          if (strncmp(event->topic, ONENET_SUB_SET_TOPIC, event->topic_len) == 0) {
    ESP_LOGI(TAG, ">>> 收到控制指令，原始数据: %.*s", event->data_len, event->data);

    cJSON *root = cJSON_ParseWithLength(event->data, event->data_len);
    if (!root) {
        ESP_LOGE(TAG, "JSON 解析失败");
        return;
    }

    // 提取消息 ID (用于回复)
    char msg_id[64] = "0";
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    if (id_item) {
        if (cJSON_IsString(id_item)) strcpy(msg_id, id_item->valuestring);
        else if (cJSON_IsNumber(id_item)) snprintf(msg_id, sizeof(msg_id), "%d", (int)id_item->valuedouble);
    }

    cJSON *params = cJSON_GetObjectItem(root, "params");
    if (params) {
        // --- 重点：兼容性解析 ---
        cJSON *relay_item = cJSON_GetObjectItem(params, PROP_RELAY);
        if (relay_item) {
            bool target_state = false;
            
            // 情况 A: 平台直接下发 {"switch_relay": true} -> 这是最常见的
            if (cJSON_IsBool(relay_item)) {
                target_state = cJSON_IsTrue(relay_item);
                ESP_LOGI(TAG, "解析成功(Bool): %s", target_state ? "ON" : "OFF");
            } 
            // 情况 B: 平台下发 {"switch_relay": {"value": true}}
            else if (cJSON_IsObject(relay_item)) {
                cJSON *v = cJSON_GetObjectItem(relay_item, "value");
                if (v && cJSON_IsBool(v)) {
                    target_state = cJSON_IsTrue(v);
                    ESP_LOGI(TAG, "解析成功(Object): %s", target_state ? "ON" : "OFF");
                }
            }

            // 执行控制
            Relay_Set_State(target_state);
            
            // --- 必须回复平台，否则平台会认为指令超时 ---
            send_set_reply(event->client, msg_id);
        }
    }
    cJSON_Delete(root);
}
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_mqtt_connected = false;
            ESP_LOGW(TAG, "❌ OneNET 断开，等待自动重连...");
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "❌ MQTT 错误事件触发");
            break;

        default:
            break;
    }
}

void MqttApp_Start(void) {
    if (s_mqtt_client != NULL) {
        esp_mqtt_client_stop(s_mqtt_client);
        esp_mqtt_client_destroy(s_mqtt_client);
        s_mqtt_client = NULL;
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    ESP_LOGI(TAG, "正在连接OneNET服务器: %s:%d", ONENET_HOST, ONENET_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .hostname = ONENET_HOST,
                .port = ONENET_PORT,
                .transport = MQTT_TRANSPORT_OVER_TCP,
            },
        },
        .credentials = {
            .client_id = ONENET_DEVICE_ID,
            .username = ONENET_PRODUCT_ID,
            .authentication = {
                .password = ONENET_TOKEN,
            },
        },
        .network = {
            .timeout_ms = 10000,
            .reconnect_timeout_ms = 3000,
            .disable_auto_reconnect = false,
        },
        .session = {
            .keepalive = 60,
            .protocol_ver = MQTT_PROTOCOL_V_3_1_1,
        },
    };

    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

// ==================== 【终极修复】手动拼接 JSON，100% 控制小数位 ====================
void MqttApp_Post_Property(bool relay_state, float voltage, float current, float power, float pf) {
    if (!s_mqtt_connected) {
        ESP_LOGW(TAG, "MQTT未连接，跳过上报");
        return;
    }

    // 1. 先四舍五入对齐
    // 电压：1位小数
    float v = (float)((int)(voltage * 10 + 0.5f)) / 10.0f;
    // 电流：3位小数
    float i = (float)((int)(current * 1000 + 0.5f)) / 1000.0f;
    // 功率：1位小数
    float p = (float)((int)(power * 10 + 0.5f)) / 10.0f;
    // 功率因数：2位小数，0-1
    float pf_align = (float)((int)(pf * 100 + 0.5f)) / 100.0f;
    if (pf_align < 0.0f) pf_align = 0.0f;
    if (pf_align > 1.0f) pf_align = 1.0f;

    // 2. 【核心】手动 sprintf 拼接 JSON，完美控制格式
    // relay: true/false
    // voltage: %.1f (1位)
    // current: %.3f (3位)
    // power: %.1f (1位)
    // power_factor: %.2f (2位)
    char payload[256];
    snprintf(payload, sizeof(payload), 
        "{\"id\":\"123\",\"params\":{"
        "\"switch_relay\":{\"value\":%s},"
        "\"voltage\":{\"value\":%.1f},"
        "\"current\":{\"value\":%.3f},"
        "\"power\":{\"value\":%.1f},"
        "\"power_factor\":{\"value\":%.2f}"
        "}}",
        relay_state ? "true" : "false",
        v, i, p, pf_align
    );

    // 3. 直接发送
    ESP_LOGI(TAG, "上报报文: %s", payload);
    int msg_id = esp_mqtt_client_publish(s_mqtt_client, ONENET_PUB_POST_TOPIC, payload, 0, 0, 0);
    if (msg_id == -1) {
        ESP_LOGE(TAG, "❌ 上报失败！");
    } else {
        ESP_LOGI(TAG, "✅ 上报成功，msg_id: %d", msg_id);
    }
}

void Wifi_Init_Station(void) {
    if (!s_is_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        esp_netif_create_default_wifi_sta();

        ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, esp_event_cb, NULL, NULL));
        ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, esp_event_cb, NULL, NULL));
        
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_wifi_init(&cfg));
        s_is_initialized = true;
    }

    wifi_config_t wifi_cfg = {0};
    strncpy((char*)wifi_cfg.sta.ssid, STATION_SSID, sizeof(wifi_cfg.sta.ssid)-1);
    strncpy((char*)wifi_cfg.sta.password, STATION_PASSWORD, sizeof(wifi_cfg.sta.password)-1);
    wifi_cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_cfg.sta.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_cfg));
    ESP_ERROR_CHECK(esp_wifi_start()); 
    esp_wifi_set_ps(WIFI_PS_NONE); 
    esp_wifi_set_max_tx_power(52); 
    s_wifi_started = true;
}