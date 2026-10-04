/**
 * @file     max98357.h
 * @brief    MAX98357A (I2S Class-D 功放) 驱动接口
 *
 * 功能：I2S 通道初始化、音频数据发送、方波测试任务
 * 修改：2026-10-03 独立成设备文件；引脚改为结构体参数注入
 * 修改：2026-10-03 适配 ESP-IDF v6.1 I2S 新 API
 */
#ifndef MAX98357_H
#define MAX98357_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MAX98357A 硬件与音频配置
 *
 * 功能：集中描述本设备所需引脚与音频参数，由 main.c 构造后注入 init
 * 修改：2026-10-03 新增，取代原先散落在 .c 里的 MAX98357_PIN_* 宏
 * 修改：2026-10-03 修正 i2s_port_t 在 v6.1 已移除，port 字段改用 int
 */
typedef struct {
    int          port;           /*!< I2S 控制器编号（v6.1 中该字段类型为 int） */
    gpio_num_t   bclk;           /*!< 位时钟脚 */
    gpio_num_t   lrc;            /*!< 声道选择/左右声道脚 */
    gpio_num_t   din;            /*!< 数据输入脚 */
    uint32_t     sample_rate_hz; /*!< 采样率 */
    uint32_t     mclk_multiple;  /*!< MCLK 相对采样率的倍数 */
    uint32_t     bclk_div;       /*!< MCLK 到 BCLK 的分频 */
    uint32_t     dma_desc_num;   /*!< DMA 描述符个数 */
    uint32_t     dma_frame_num;  /*!< 单个 DMA 缓冲的帧数 */
    uint32_t     beep_freq_hz;   /*!< 测试方波频率 */
    uint32_t     beep_ms;        /*!< 单次方波时长 */
    uint32_t     task_period_ms; /*!< 重复播放间隔 */
} max98357_pins_t;

/**
 * @brief  初始化功放：分配 I2S 通道、配置时钟槽位与引脚、启用输出
 *
 * 功能：完成本设备全部引脚初始化，并在成功时打印实际生效的引脚
 * 修改：2026-10-03 改为接收 max98357_pins_t 参数，引脚不再硬编码
 * 修改：2026-10-03 迁移到 v6.1 API：i2s_new_channel / i2s_channel_init_std_mode
 *
 * @param pins  引脚与音频配置，不可为 NULL
 * @return ESP_OK 成功
 */
esp_err_t max98357_init(const max98357_pins_t *pins);

/**
 * @brief  发送 PCM 音频数据
 *
 * 功能：阻塞写入 I2S DMA
 * 修改：2026-10-03 timeout 参数语义改为 v6.1 的 uint32_t 毫秒
 *
 * @param data       采样数据
 * @param bytes      字节数，必须为 2 的倍数（16 bit）
 * @param timeout_ms 超时毫秒，传 UINT32_MAX 表示永久等待
 */
esp_err_t max98357_play(const int16_t *data, size_t bytes, uint32_t timeout_ms);

/**
 * @brief  播放一次方波（频率/时长取自 pins）
 *
 * 功能：生成方波并送入功放，用于验证音频链路
 * 修改：2026-10-03 新增
 */
esp_err_t max98357_play_beep(void);

/**
 * @brief  启动后台播放任务
 *
 * 功能：按 pins->task_period_ms 周期重复播放方波
 * 修改：2026-10-03 新增
 */
esp_err_t max98357_start_task(void);

/**
 * @brief  查询功放是否初始化成功
 *
 * 功能：状态查询
 * 修改：2026-10-03 新增
 */
bool max98357_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* MAX98357_H */