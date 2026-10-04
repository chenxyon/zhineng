/**
 * @file     inmp441.h
 * @brief    INMP441 I2S 数字 MEMS 麦克风驱动接口
 *
 * 功能：I2S 采集任务管理、PCM 缓冲管理、唤醒/STT 数据源
 * 修改：2026-10-04 新建
 *
 * @note     本驱动与 max98357 共用 I2S_NUM_0（全双工）：
 *           - 功放数据出=dout(GPIO37)
 *           - 麦克风数据入=din(GPIO17)
 *           共用 BCLK(GPIO35) 与 LRC(GPIO36)
 *
 * @note     INMP441 硬件要求：
 *           - WS 脚硬接 3.3V（左声道）或 GND（右声道），悬空产生噪声
 *           - VDD 接 3.3V，与 ESP32 共地
 *           - SD 脚输出 24-bit 数据，按 16-bit 位对齐读取（左声道在低 24 bit）
 */
#ifndef INMP441_H
#define INMP441_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 采集配置
 *
 * 功能：描述本板麦克风采集参数，由 board.c 构造后注入
 * 修改：2026-10-04 新增
 */
typedef struct {
    uint32_t sample_rate_hz;   /*!< 采样率，建议 16000（唤醒词标准） */
    uint32_t frame_ms;         /*!< 每帧时长（ms），建议 50 = 800 采样 */
    uint8_t  channels;        /*!< 声道数，INMP441 固定 1 */
} inmp441_cfg_t;

/**
 * @brief  初始化 INMP441 采集
 *
 * 功能：启动 I2S 采集任务，持续写入内部环形缓冲
 * 修改：2026-10-04 新建
 *
 * @param cfg  采集参数
 * @return ESP_OK 成功
 */
esp_err_t inmp441_init(const inmp441_cfg_t *cfg);

/**
 * @brief  停止 INMP441 采集
 *
 * 功能：停止采集任务
 * 修改：2026-10-04 新建
 */
void inmp441_stop(void);

/**
 * @brief  读取最近 N 字节 PCM
 *
 * 功能：从环形缓冲取数据，供 ESP-SR 唤醒 / 在线 STT 使用
 * 修改：2026-10-04 新建
 *
 * @param buf    接收缓冲
 * @param bytes  需要读取的字节数（必须为 2 的倍数）
 * @param out    实际读到的字节数（可 NULL）
 * @param timeout_ms 等待有数据的超时；0=非阻塞
 * @return ESP_OK 成功，ESP_ERR_TIMEOUT 超时
 */
esp_err_t inmp441_read(int16_t *buf, size_t bytes, size_t *out, uint32_t timeout_ms);

/**
 * @brief  查询采集任务是否运行中
 *
 * 功能：状态查询
 * 修改：2026-10-04 新建
 */
bool inmp441_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* INMP441_H */