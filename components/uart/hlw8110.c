#include "hlw8110.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "HLW8110_DRV";

static uint8_t HLW_CalcChecksum(uint8_t *data, uint8_t len) {
    uint32_t sum = 0;
    for(uint8_t i=0; i<len; i++) sum += data[i];
    return (~sum) & 0xFF;
}

static void HLW_SendSpecialCmd(uint8_t cmd_data) {
    uint8_t cmd[4] = {0xA5, 0xEA, cmd_data, 0x00};
    cmd[3] = HLW_CalcChecksum(cmd, 3);
    uart_write_bytes(HLW_UART_PORT_NUM, (const char*)cmd, 4);
    vTaskDelay(pdMS_TO_TICKS(10));
}

static void HLW_WriteReg(uint8_t reg_addr, uint16_t data) {
    HLW_SendSpecialCmd(HLW_CMD_WRITE_EN);
    uint8_t cmd[6] = {0xA5, (1<<7)|reg_addr, (data>>8)&0xFF, data&0xFF, 0x00};
    cmd[4] = HLW_CalcChecksum(cmd, 4);
    uart_write_bytes(HLW_UART_PORT_NUM, (const char*)cmd, 5);
    vTaskDelay(pdMS_TO_TICKS(10));
    HLW_SendSpecialCmd(HLW_CMD_WRITE_LOCK);
}

static esp_err_t HLW_ReadReg(uint8_t reg_addr, uint8_t read_len, uint8_t *out_buf) {
    uart_flush_input(HLW_UART_PORT_NUM);
    uint8_t cmd[2] = {0xA5, reg_addr};
    uart_write_bytes(HLW_UART_PORT_NUM, (const char*)cmd, 2);

    uint8_t recv[5] = {0};
    int len = uart_read_bytes(HLW_UART_PORT_NUM, recv, read_len + 1, pdMS_TO_TICKS(100));
    if (len != (read_len + 1)) return ESP_ERR_TIMEOUT;

    uint32_t sum = 0xA5 + reg_addr;
    for(int i=0; i<read_len; i++) sum += recv[i];
    if (((~sum) & 0xFF) != recv[read_len]) return ESP_FAIL;

    memcpy(out_buf, recv, read_len);
    return ESP_OK;
}

esp_err_t HLW8110_Init(void) {
    // 检查并清理可能存在的旧 UART 驱动，防止内存冲突
    if (uart_is_driver_installed(HLW_UART_PORT_NUM)) {
        uart_driver_delete(HLW_UART_PORT_NUM);
    }

    uart_config_t uart_config = {
        .baud_rate = 9600,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_EVEN,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 安装驱动
    ESP_ERROR_CHECK(uart_driver_install(HLW_UART_PORT_NUM, 256, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(HLW_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(HLW_UART_PORT_NUM, HLW_TX_PIN, HLW_RX_PIN, -1, -1));

    vTaskDelay(pdMS_TO_TICKS(100));
    HLW_SendSpecialCmd(HLW_CMD_RESET);
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 写入配置寄存器
    HLW_WriteReg(HLW_REG_SYSCON, 0x0A04); // 电流16x, 电压1x增益
    HLW_WriteReg(HLW_REG_EMUCON, 0x0001); // 开启计算
    // 【新增】配置 EMUCON2 (0x13): 
    // Bit 6 = 1 (开启 PF 输出)
    // Bit 0 = 1 (确保使用内置 1.25V 参考源)
    HLW_WriteReg(HLW_REG_EMUCON2, 0x0041);
    ESP_LOGI(TAG, "HLW8110 Initialized Successfully!");
    return ESP_OK;
}

esp_err_t HLW8110_ReadAll(hlw8110_data_t *result) {
    if(!result) return ESP_ERR_INVALID_ARG;
    uint8_t b[4];
    uint32_t raw;

    // 1. 读电压
    if(HLW_ReadReg(HLW_REG_RMS_U, 3, b) == ESP_OK) {
        raw = ((uint32_t)b[0]<<16) | ((uint32_t)b[1]<<8) | b[2];
        result->voltage_V = ((float)raw * HLW_RMS_UC) / 4194304.0f * 0.01f;
    } else return ESP_FAIL;

    // 2. 读电流
    if(HLW_ReadReg(HLW_REG_RMS_IA, 3, b) == ESP_OK) {
        raw = ((uint32_t)b[0]<<16) | ((uint32_t)b[1]<<8) | b[2];
        result->current_A = ((float)raw * HLW_RMS_IAC) / 8388608.0f * 0.001f;
    }

    // 3. 读功率
    if(HLW_ReadReg(HLW_REG_POWER_PA, 4, b) == ESP_OK) {
        int32_t p_raw = ((int32_t)b[0]<<24) | ((int32_t)b[1]<<16) | ((int32_t)b[2]<<8) | b[3];
        float p_abs = (p_raw < 0) ? (float)(-p_raw) : (float)p_raw;
        result->power_W = (p_abs * HLW_POWER_PAC) / 2147483648.0f;
    }

 if(HLW_ReadReg(HLW_REG_PF, 3, b) == ESP_OK) {
    // 合并 24 位数据
    int32_t pf_raw = ((int32_t)b[0] << 16) | ((int32_t)b[1] << 8) | b[2];
    
    // 符号扩展（处理负值，即感性/容性负载方向）
    if (pf_raw & 0x800000) {
        pf_raw |= 0xFF000000;
    }

    // 根据手册，0x7FFFFF (8388607) 对应 PF = 1.0 
    float pf_val = (float)pf_raw / 8388607.0f;
    
    // 取绝对值作为显示的 PF
    result->pf = (pf_val < 0) ? -pf_val : pf_val;

    if (result->pf > 1.0f) result->pf = 1.00f;
}

// 5. 小电流消隐逻辑
if(result->current_A < 0.01f) { // 阈值调小一点，0.03A 有点高
    result->current_A = 0.0f;
    result->power_W = 0.0f;
    result->pf = 0.0f;
}
    
    return ESP_OK;
}