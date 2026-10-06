/**
 * @file     max98357.h
 * @brief    MAX98357 (I2S Class-D 功放) 驱动接口
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
 * 音符频率定义（Hz）
 *
 * 功能：标准十二平均律，供 melody 数组引用
 * 修改：2026-10-06 新增，参照 Arduino 音频示例
 */
#define NOTE_C4  261.63f
#define NOTE_D4  293.66f
#define NOTE_E4  329.63f
#define NOTE_F4  349.23f
#define NOTE_G4  392.00f
#define NOTE_A4  440.00f
#define NOTE_B4  493.88f
#define NOTE_C5  523.25f

/**
 * 节拍时长定义（ms，BPM ≈ 120）
 *
 * 功能：供 melody 数组引用
 * 修改：2026-10-06 新增
 */
#define BEAT_QUARTER  250u   /* 四分音符 */
#define BEAT_EIGHTH   125u   /* 八分音符 */
#define BEAT_HALF     500u   /* 二分音符 */
#define BEAT_WHOLE   1000u   /* 全音符 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * MAX98357 硬件与音频配置
 *
 * 功能：集中描述本设备所需引脚与音频参数，由 board.c 构造后注入 init
 * 修改：2026-10-03 新增，取代原先散落在 .c 里的 MAX98357_PIN_* 宏
 * 修改：2026-10-04 新增 mic_din 字段；I2S 改为全双工，dout=功放、din=麦克风
 */
typedef struct {
    int          port;           /*!< I2S 控制器编号（v6.1 中该字段类型为 int） */
    gpio_num_t   bclk;           /*!< 位时钟脚 */
    gpio_num_t   lrc;            /*!< 声道选择/左右声道脚 */
    gpio_num_t   din;            /*!< 数据输出脚（ESP32 → 功放） */
    gpio_num_t   mic_din;        /*!< 数据输入脚（麦克风 → ESP32），全双工 */
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
 * @brief  读取麦克风 PCM（I2S 全双工接收）
 *
 * 功能：全双工模式必须与写配对消费，否则接收 DMA 满溢出
 * 修改：2026-10-04 新增
 */
esp_err_t max98357_mic_read(int16_t *data, size_t bytes, size_t *written, uint32_t timeout_ms);

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
 * 功能：按 pins->task_period_ms 周期检查自测开关，决定是否播放方波
 * 修改：2026-10-03 新增
 * 修改：2026-10-04 改为受 max98357_set_selftest_beep() 控制，默认不响
 */
esp_err_t max98357_start_task(void);

/**
 * @brief  开关周期性自测方波
 *
 * 功能：仅用于验证音频链路。正式功能接入 TTS 后应保持关闭，
 *       否则会与语音播报抢占 I2S 通道
 * 修改：2026-10-04 新增
 *
 * @param enable true 开启周期方波，false 静音
 */
void max98357_set_selftest_beep(bool enable);

/**
 * @brief  查询功放是否初始化成功
 *
 * 功能：状态查询
 * 修改：2026-10-03 新增
 */
bool max98357_is_ready(void);

/* ==================== 正弦波 / 旋律播放（2026-10-06 新增） ==================== */

/**
 * @brief  音符结构体
 *
 * 功能：描述一段旋律中的一个音
 * 修改：2026-10-06 新增
 */
typedef struct {
    float  freq;        /*!< 频率（Hz），可直接用 NOTE_C4 等宏 */
    uint32_t duration_ms;  /*!< 时长（毫秒）*/
} max98357_note_t;

/**
 * @brief  播放单个正弦波音符
 *
 * 功能：生成指定频率的正弦波 PCM，阻塞发送直到播放完毕
 *
 * @param freq       频率（Hz），传 0 表示静音
 * @param duration_ms  时长（毫秒），0 表示使用 beep_ms 默认值
 * @return ESP_OK 成功
 */
esp_err_t max98357_play_tone(float freq, uint32_t duration_ms);

/**
 * @brief  播放预设旋律
 *
 * 功能：按音符数组依次播放正弦波音符，阻塞直到全部播完
 *
 * @param melody   音符数组（必须以 {.freq=0, .duration_ms=0} 结尾）
 * @return ESP_OK 成功
 *
 * 示例（小星星）：
 *   max98357_note_t twinkle[] = {
 *       { NOTE_C4, BEAT_QUARTER }, { NOTE_C4, BEAT_QUARTER },
 *       { NOTE_G4, BEAT_QUARTER }, { NOTE_G4, BEAT_QUARTER },
 *       ...
 *       { 0, 0 }  // 终止符
 *   };
 *   max98357_play_melody(twinkle);
 */
esp_err_t max98357_play_melody(const max98357_note_t *melody);

#ifdef __cplusplus
}
#endif

#endif /* MAX98357_H */