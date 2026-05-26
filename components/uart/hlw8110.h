#ifndef __HLW8110_H
#define __HLW8110_H

#include <stdint.h>
#include "esp_err.h"

// ================= 硬件引脚配置 =================
#define HLW_UART_PORT_NUM      UART_NUM_1
#define HLW_TX_PIN             21  
#define HLW_RX_PIN             20  
#define HLW_BAUD_RATE          9600

// ================= 核心校准系数 (基于1M:1K分压) =================
// 修正了电压和电流的校准系数，确保测量结果更准确
#define HLW_RMS_UC             41782.0f
#define HLW_RMS_IAC            52162.0f
#define HLW_POWER_PAC          43533.0f

// ================= 寄存器地址 =================
#define HLW_REG_SYSCON         0x00
#define HLW_REG_EMUCON         0x01
#define HLW_REG_EMUCON2        0x13
#define HLW_REG_RMS_IA         0x24
#define HLW_REG_RMS_U          0x26
#define HLW_REG_POWER_PA       0x2C
#define HLW_CMD_WRITE_EN       0xE5
#define HLW_CMD_WRITE_LOCK     0xDC
#define HLW_CMD_RESET          0x96
// hlw8110.h
#define HLW_REG_PF    0x27   

typedef struct {
    float voltage_V;
    float current_A;
    float power_W;
    float pf;        
} hlw8110_data_t;

esp_err_t HLW8110_Init(void);
esp_err_t HLW8110_ReadAll(hlw8110_data_t *result);

#endif