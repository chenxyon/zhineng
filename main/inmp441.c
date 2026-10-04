/**
 * @file     inmp441.c
 * @brief    INMP441 I2S 数字 MEMS 麦克风驱动
 *
 * 功能：I2S 采集任务管理、PCM 循环缓冲、数据供给唤醒/STT
 * 修改：2026-10-04 新建
 * 修改：2026-10-04 环形缓冲改用自含循环缓冲 + 二元互斥锁，
 *         移除对 freertos/ringbuf.h 的依赖（ESP-IDF 6.1 该 API 位置变动）
 *
 * @note     本驱动复用 max98357 已初始化的 I2S_NUM_0 全双工控制器。
 *           调用顺序必须：**先 max98357_init()，再 inmp441_init()**。
 *           采集任务通过 max98357_mic_read() 读入麦克风数据，持续排空
 *           接收 DMA，防止全双工模式下接收端溢出。
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"

#include "inmp441.h"
#include "max98357.h"

static const char *TAG = "MIC";

/* 循环缓冲：16384 个 16-bit 单声道采样 = 32 KB ≈ 743 ms @22050 Hz */
#define MIC_BUF_SAMPLES  16384
#define MIC_BUF_MASK     (MIC_BUF_SAMPLES - 1)

/* 每次从 I2S 读入一块的采样数（1024 字节 = 512 采样） */
#define MIC_BLOCK_SAMPLES  512

static TaskHandle_t        s_task  = NULL;
static SemaphoreHandle_t   s_mutex = NULL;
static volatile bool       s_running = false;
static inmp441_cfg_t       s_cfg;
static inmp441_pins_t      s_pins;

/* 循环缓冲状态（均须持 s_mutex 访问） */
static int16_t   s_ring[MIC_BUF_SAMPLES];
static uint32_t  s_head = 0;   /*!< 下一个写入位置 */
static uint32_t  s_tail = 0;   /*!< 最旧有效样本位置 */
static uint32_t  s_count = 0;  /*!< 有效样本数 */

/**
 * @brief  采集任务体
 *
 * 功能：循环从 I2S 全双工接收端读 INMP441 数据，写入循环缓冲
 * 修改：2026-10-04 新建
 */
static void mic_task(void *arg)
{
    (void)arg;

    int16_t block[MIC_BLOCK_SAMPLES];

    while (s_running) {
        size_t written = 0;

        /* 全双工模式：必须持续读取接收端，否则 DMA 堆积溢出。
           用 200ms 超时，失败则短延时重试避免空转。 */
        esp_err_t ret = max98357_mic_read(block, sizeof(block), &written, 200);
        if (ret != ESP_OK || written == 0) {
            vTaskDelay(pdMS_TO_TICKS(2));
            continue;
        }

        uint32_t n = written / sizeof(int16_t);
        if (n > MIC_BLOCK_SAMPLES) {
            n = MIC_BLOCK_SAMPLES;
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        for (uint32_t i = 0; i < n; i++) {
            /* 缓冲满时丢弃最旧样本，保证写入位置始终最新 */
            if (s_count == MIC_BUF_SAMPLES) {
                s_tail++;
            } else {
                s_count++;
            }
            s_ring[s_head & MIC_BUF_MASK] = block[i];
            s_head++;
        }
        xSemaphoreGive(s_mutex);
    }

    vTaskDelete(NULL);
}

esp_err_t inmp441_init(const inmp441_pins_t *pins, const inmp441_cfg_t *cfg)
{
    if (pins == NULL || cfg == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_running) {
        return ESP_OK;
    }

    s_pins = *pins;
    s_cfg  = *cfg;
    if (s_cfg.channels == 0) {
        s_cfg.channels = 1;
    }

    s_head  = 0;
    s_tail  = 0;
    s_count = 0;
    s_mutex = xSemaphoreCreateBinary();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "互斥锁创建失败");
        return ESP_ERR_NO_MEM;
    }
    xSemaphoreGive(s_mutex);

    s_running = true;
    BaseType_t ok = xTaskCreate(mic_task, "mic", 4096, NULL, 5, &s_task);
    if (ok != pdPASS) {
        s_running = false;
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
        ESP_LOGE(TAG, "采集任务创建失败");
        return ESP_ERR_NO_MEM;
    }

    /* 缓冲时长（ms）= 样本数 * 1000 / 采样率 */
    ESP_LOGI(TAG, "INMP441 就绪 -> SCK=%d WS=%d SD=%d 采样率=%u 缓冲≈%d ms",
             (int)s_pins.sck, (int)s_pins.ws, (int)s_pins.sd,
             (unsigned)s_cfg.sample_rate_hz,
             (int)((uint64_t)MIC_BUF_SAMPLES * 1000 / (s_cfg.sample_rate_hz ? s_cfg.sample_rate_hz : 1)));
    return ESP_OK;
}

void inmp441_stop(void)
{
    if (!s_running) {
        return;
    }

    s_running = false;

    /* 等待任务自行退出（最坏 500ms） */
    for (int i = 0; i < 50 && s_task != NULL; i++) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    if (s_mutex != NULL) {
        vSemaphoreDelete(s_mutex);
        s_mutex = NULL;
    }
    s_task = NULL;

    ESP_LOGI(TAG, "INMP441 采集已停止");
}

esp_err_t inmp441_read(int16_t *buf, size_t bytes, size_t *out, uint32_t timeout_ms)
{
    if (!s_running || s_mutex == NULL || buf == NULL || bytes == 0 || (bytes % sizeof(int16_t)) != 0) {
        return ESP_ERR_INVALID_STATE;
    }

    size_t need = bytes / sizeof(int16_t);
    size_t got  = 0;
    bool timed_out = false;

    while (got < need) {
        /* 等待有数据；timeout_ms 为 0 表示非阻塞（立即返回） */
        TickType_t start = xTaskGetTickCount();
        for (;;) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            bool has_data = (s_count > 0);
            xSemaphoreGive(s_mutex);
            if (has_data) {
                break;
            }
            if (timeout_ms == 0) {
                timed_out = true;
                break;
            }
            if (xTaskGetTickCount() - start >= pdMS_TO_TICKS(timeout_ms)) {
                timed_out = true;
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (timed_out) {
            break;
        }

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        uint32_t take_n = (s_count < need - got) ? s_count : (uint32_t)(need - got);
        for (uint32_t i = 0; i < take_n; i++) {
            buf[got + i] = s_ring[s_tail & MIC_BUF_MASK];
            s_tail++;
        }
        s_count -= take_n;
        got += take_n;
        xSemaphoreGive(s_mutex);
    }

    if (out != NULL) {
        *out = got * sizeof(int16_t);
    }
    return (got == need) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool inmp441_is_running(void)
{
    return s_running;
}