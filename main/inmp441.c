/**
 * @file     inmp441.c
 * @brief    INMP441 I2S 数字 MEMS 麦克风驱动
 *
 * 功能：I2S 采集任务管理、PCM 环形缓冲、数据供给唤醒/STT
 * 修改：2026-10-04 新建
 *
 * @note     本驱动复用 max98357 已初始化的 I2S_NUM_0 全双工控制器。
 *           因此调用顺序必须：**先 max98357_init()，再 inmp441_init()**。
 *           采集任务通过 max98357_mic_read() 读入麦克风数据。
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#include "esp_log.h"

#include "inmp441.h"
#include "max98357.h"

static const char *TAG = "MIC";

#define MIC_BUF_BLOCK_BYTES  (1024)  /*!< 每块 1024 字节 = 512 个 16-bit 采样 */
#define MIC_BUF_DEPTH        (8)     /*!< 环形缓冲 8 块 = 4096 字节 ≈ 186 ms @22.05kHz */

static TaskHandle_t  s_task   = NULL;
static RingbufHandle_t s_buf  = NULL;
static bool           s_running = false;
static inmp441_cfg_t  s_cfg;

/**
 * @brief  采集任务体
 *
 * 功能：循环从 I2S 全双工接收端读 INMP441 数据，写入环形缓冲
 * 修改：2026-10-04 新建
 */
static void mic_task(void *arg)
{
    (void)arg;

    int16_t block[MIC_BUF_BLOCK_BYTES / sizeof(int16_t)];
    size_t  written;

    while (s_running) {
        /* 用功放驱动暴露的全双工读接口取麦克风 PCM。
           全双工模式：写功放（dout）与读麦克风（din）必须持续配对，
           否则接收 DMA 堆积。这里不写功放，只读，保证接收端有数据。 */
        esp_err_t ret = max98357_mic_read(block, sizeof(block), &written, 1000);
        if (ret == ESP_OK && written > 0) {
            size_t copy_len = (written < sizeof(block)) ? written : sizeof(block);
            int16_t *slice = (int16_t *)heap_caps_malloc(copy_len, MALLOC_CAP_INTERNAL);
            if (slice != NULL) {
                memcpy(slice, block, copy_len);
                if (xRingbufSend(s_buf, slice, 0) != pdPASS) {
                    free(slice);   /* 缓冲满时丢弃一块，避免阻塞 */
                }
            }
        } else {
            /* 读取超时或失败：稍等再试，避免空转 */
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }

    vTaskDelete(NULL);
}

esp_err_t inmp441_init(const inmp441_cfg_t *cfg)
{
    if (cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_running) {
        return ESP_OK;
    }

    s_cfg = *cfg;
    if (s_cfg.channels == 0) {
        s_cfg.channels = 1;
    }

    /* 环形缓冲：8 块 × 1024 字节 = 4096 字节 ≈ 186 ms @22.05 kHz */
    s_buf = xRingbufCreate(MIC_BUF_BLOCK_BYTES, MIC_BUF_DEPTH);
    if (s_buf == NULL) {
        ESP_LOGE(TAG, "环形缓冲创建失败（内存不足）");
        return ESP_ERR_NO_MEM;
    }

    s_running = true;
    BaseType_t ok = xTaskCreate(mic_task, "mic", 4096, NULL, 5, &s_task);
    if (ok != pdPASS) {
        s_running = false;
        vRingbufDelete(s_buf);
        s_buf = NULL;
        ESP_LOGE(TAG, "采集任务创建失败");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "INMP441 采集就绪 -> 采样率=%u 每帧=%u ms 缓冲≈%d ms",
             (unsigned)s_cfg.sample_rate_hz, (unsigned)s_cfg.frame_ms,
             (int)(MIC_BUF_BLOCK_BYTES * MIC_BUF_DEPTH / 2 / s_cfg.sample_rate_hz * 1000));
    return ESP_OK;
}

void inmp441_stop(void)
{
    if (!s_running) {
        return;
    }

    s_running = false;

    /* 等待任务自行退出，最多 500ms */
    for (int i = 0; i < 50 && s_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_buf != NULL) {
        vRingbufDelete(s_buf);
        s_buf = NULL;
    }
    s_task = NULL;

    ESP_LOGI(TAG, "INMP441 采集已停止");
}

esp_err_t inmp441_read(int16_t *buf, size_t bytes, size_t *out, uint32_t timeout_ms)
{
    if (!s_running || s_buf == NULL || buf == NULL || bytes == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t read = 0;
    size_t chunk = 0;

    while (read < bytes) {
        int16_t *block = NULL;
        BaseType_t ret = xRingbufReceive(s_buf, &block, (portBASE_TYPE)(timeout_ms ? 100 : 0));
        if (ret != pdPASS || block == NULL) {
            break;   /* 缓冲空且等待超时 */
        }

        size_t to_copy = (bytes - read < sizeof(int16_t) * (MIC_BUF_BLOCK_BYTES / sizeof(int16_t)))
                           ? (bytes - read) : MIC_BUF_BLOCK_BYTES;
        if (to_copy > bytes - read) {
            to_copy = bytes - read;
        }
        memcpy(buf + read / sizeof(int16_t), block, to_copy);
        read += to_copy;
        free(block);
        chunk++;
    }

    if (out != NULL) {
        *out = read;
    }
    return (read == bytes) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool inmp441_is_running(void)
{
    return s_running;
}