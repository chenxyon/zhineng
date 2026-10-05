/**
 * @file     max98357.c
 * @brief    MAX98357 (I2S Class-D 功放) 驱动实现
 *
 * 功能：I2S 通道初始化、音频发送、方波生成与后台测试任务
 * 修改：2026-10-03 按 ESP-IDF v6.1 I2S 新 API 重写，修复以下 18 处编译错误：
 *         i2s_channel_alloc -> i2s_new_channel；.chan_id -> .id；
 *         .dma_buf_size -> .dma_frame_num；移除 .use_apll/.mclk_pin_sel/.intr_alloc_flags；
 *         i2s_channel_init_std -> i2s_channel_init_std_mode；
 *         .sample_rate -> .sample_rate_hz；.mclk_hz/.clkm_div -> .mclk_multiple/.bclk_div；
 *         .data_width -> .data_bit_width；.std_mode -> .slot_mode；
 *         .bclk_pin_num/.ws_pin_num/.dout_pin_num/.din_pin_num -> .bclk/.ws/.dout/.din
 * 修改：2026-10-03 引脚改为由 max98357_pins_t 参数注入
 * 修改：2026-10-03 方波频率由硬编码周期 50 改为按 pins->beep_freq_hz 计算
 * 修改：2026-10-04 改为 I2S 全双工：i2s_new_channel 同时分配 TX(功放)+RX(麦克风)
 *         两个句柄，两通道都 init/enable；max98357_mic_read 改用 RX 句柄读取。
 *         此前传 NULL 只建 TX 通道，RX 读取报 "this channel is not rx channel"
 */
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/i2s_common.h"
#include "driver/i2s_std.h"

#include "max98357.h"

static const char *TAG = "AUDIO";

static i2s_chan_handle_t s_spk    = NULL;  /*!< 发送通道（功放） */
static i2s_chan_handle_t s_mic_rx = NULL;  /*!< 接收通道（INMP441），与 s_spk 同一 I2S 外设全双工 */
static TaskHandle_t      s_task   = NULL;
static bool              s_ready  = false;
static bool              s_selftest_beep = false;  /*!< 自测方波开关，默认关 */
static max98357_pins_t   s_cfg;

esp_err_t max98357_init(const max98357_pins_t *pins)
{
    if (pins == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }
    s_cfg = *pins;

    i2s_chan_config_t chan_cfg = {
        .id            = pins->port,
        .role          = I2S_ROLE_MASTER,
        .dma_desc_num  = pins->dma_desc_num,
        .dma_frame_num = pins->dma_frame_num,
        .auto_clear    = true,
        .allow_pd      = false,
    };
    /* 全双工：一次分配 TX（功放）+ RX（麦克风）两个通道句柄到同一 I2S 外设。
       第二个参数传 NULL 会得到纯 TX 通道，RX 读取会报 "not rx channel" */
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_spk, &s_mic_rx));

    i2s_std_config_t std_cfg = {
        .clk_cfg = {
            .sample_rate_hz = pins->sample_rate_hz,
            .clk_src        = I2S_CLK_SRC_DEFAULT,
            .mclk_multiple  = pins->mclk_multiple,
            .bclk_div       = pins->bclk_div,
        },
        .slot_cfg = {
            .data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
            .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
            .slot_mode      = I2S_SLOT_MODE_MONO,
            .slot_mask      = I2S_STD_SLOT_LEFT,
            .ws_width       = 16,
            .ws_pol         = false,
            .bit_shift      = false,     /* MAX98357A 要求标准 I2S 时序；true 会移位导致无声 */
        },
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = pins->bclk,
            .ws   = pins->lrc,
            .dout = pins->din,      /* 功放数据脚（ESP32 输出） */
            .din  = pins->mic_din,  /* 麦克风数据脚（ESP32 输入） */
        },
    };
    /* 全双工两个通道都要初始化、都要使能（参照 IDF i2s_usb 示例）。
       MCLK 由后初始化的通道产生，先 init RX 再 init TX，使 MCLK 来自功放侧 */
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_mic_rx, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_spk, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(s_mic_rx));
    ESP_ERROR_CHECK(i2s_channel_enable(s_spk));

    s_ready = true;
    ESP_LOGI(TAG, "初始化完成 -> BCLK=%d LRC=%d 功放=%d 麦克风=%d 采样率=%u",
             (int)pins->bclk, (int)pins->lrc, (int)pins->din, (int)pins->mic_din,
             (unsigned)pins->sample_rate_hz);
    return ESP_OK;
}

esp_err_t max98357_play(const int16_t *data, size_t bytes, uint32_t timeout_ms)
{
    if (!s_ready || data == NULL || bytes == 0 || (bytes % sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t written = 0;
    return i2s_channel_write(s_spk, data, bytes, &written, timeout_ms);
}

/**
 * @brief  读取麦克风 PCM（I2S 全双工接收）
 *
 * 功能：从 I2S 接收 DMA 读 INMP441 数据；全双工模式必须与写配对消费，
 *       否则接收缓冲满溢出
 * 修改：2026-10-04 新增，配合 INMP441 数据脚（GPIO17）
 *
 * @param data       接收 PCM 缓冲区（int16_t 对齐）
 * @param bytes      字节数，必须为 2 的倍数
 * @param written    实际读取字节数（输出，可传 NULL）
 * @param timeout_ms 超时毫秒
 */
esp_err_t max98357_mic_read(int16_t *data, size_t bytes, size_t *written, uint32_t timeout_ms)
{
    if (!s_ready || data == NULL || bytes == 0 || (bytes % sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    size_t read_cnt = 0;
    esp_err_t ret = i2s_channel_read(s_mic_rx, data, bytes, &read_cnt, timeout_ms);
    if (written != NULL) {
        *written = read_cnt;
    }
    return ret;
}

esp_err_t max98357_play_beep(void)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    /* 半个周期占用的采样点数 -> 实际频率 = 采样率 / (2 * half_samples) */
    const uint32_t half_samples = s_cfg.sample_rate_hz / (2 * s_cfg.beep_freq_hz);
    const int      samples     = (int)(s_cfg.sample_rate_hz * s_cfg.beep_ms / 1000);
    const int16_t  amp         = 8000;

    if (half_samples == 0 || samples <= 0) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t *beep = malloc((size_t)samples * sizeof(int16_t));
    if (beep == NULL) {
        ESP_LOGE(TAG, "方波缓冲区分配失败 (%d 字节)", samples * (int)sizeof(int16_t));
        return ESP_ERR_NO_MEM;
    }

    for (int i = 0; i < samples; i++) {
        beep[i] = (i % (int)half_samples < (int)(half_samples / 2)) ? amp : -amp;
    }

    esp_err_t ret = max98357_play(beep, (size_t)samples * sizeof(int16_t), UINT32_MAX);
    free(beep);

    ESP_LOGI(TAG, "播放 %u Hz 方波 (%u ms) -> 喇叭响一下",
             (unsigned)s_cfg.beep_freq_hz, (unsigned)s_cfg.beep_ms);
    return ret;
}

/**
 * @brief  任务体：周期性播放方波
 *
 * 功能：后台持续验证音频链路
 * 修改：2026-10-03 改为读取 s_cfg 中的参数，不再引用全局宏
 * 修改：2026-10-04 增加自测开关，默认关闭。上电持续「滴」声干扰配网与
 *         对话调试，等阶段 7 接入 TTS 后由语音输出取代此测试音
 */
static void audio_task(void *arg)
{
    for (;;) {
        if (s_selftest_beep) {
            max98357_play_beep();
        }
        vTaskDelay(pdMS_TO_TICKS(s_cfg.task_period_ms));
    }
}

void max98357_set_selftest_beep(bool enable)
{
    s_selftest_beep = enable;
}

esp_err_t max98357_start_task(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return xTaskCreate(audio_task, "audio", 4096, NULL, 5, &s_task) == pdPASS
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

bool max98357_is_ready(void)
{
    return s_ready;
}