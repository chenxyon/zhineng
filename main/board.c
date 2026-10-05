/**
 * @file     board.c
 * @brief    板级装配层 —— 引脚定义与设备初始化
 *
 * 功能：本板全部引脚定义、引脚表打印、设备初始化
 * 修改：2026-10-03 从 main.c 拆出，main.c 仅保留串行调用
 *
 * @note     引脚复用策略（千问方案修改版）
 *           OLED SPI: SCK=GPIO13 (避开 GPIO6 内部 Flash)
 *                     MOSI=GPIO10 (避开 GPIO7=0.91 OLED SDA)
 *                     CS=GPIO5,  DC=GPIO3, RST=GPIO4
 *                     (⚠ GPIO3/4/5 是 strap 脚，仅上电采样前需高阻)
 *           W25Q128  : CS=GPIO39, SCK=GPIO40, MOSI=GPIO41, MISO=GPIO42
 *           I2S 全双工（PSRAM 未启用，35~37 可用）：
 *           共用时钟：BCLK=GPIO35, LRC=GPIO36
 *                     MAX98357A 数据出=dout=GPIO37
 *                     INMP441   数据入=din =GPIO17
 *           （原 18/19/20 为误配，已更正为 35/36/37）
 */
#include <stdint.h>

#include "esp_log.h"
#include "driver/gpio.h"

#include "board.h"

static const char *TAG = "BOARD";

/* ====================== 本板引脚定义（唯一可信来源） ====================== */

/**
 * OLED SSD1306 0.96" SPI 接线
 *
 * 功能：声明本板 OLED 的实际引脚
 * 修改：2026-10-03 新增，作为装配层注入值
 */
static const oled_spi_pins_t k_oled_pins = {
    .host             = SPI2_HOST,
    .sck              = GPIO_NUM_13,
    .mosi             = GPIO_NUM_10,
    .cs               = GPIO_NUM_5,
    .dc               = GPIO_NUM_3,
    .rst              = GPIO_NUM_4,
    .clk_hz           = 4 * 1000 * 1000,
    .max_transfer_sz  = 1024,
};

/**
 * MAX98357A I2S Class-D 功放接线
 *
 * 功能：声明本板功放的实际引脚与音频参数
 * 修改：2026-10-03 新增，字段按 ESP-IDF v6.1 I2S API 调整
 * 修改：2026-10-04 引脚由 18/19/20 更正为 35/36/37（实际硬件接线）；
 *         新增 mic_din 字段（GPIO17，INMP441 数据输入，I2S 全双工共用控制器）
 * 修改：2026-10-05 从原 35/36/37 换为 20/19/18；BCLK=DIN 互换，
 *         现 BCLK=GPIO20, LRC=GPIO19, DIN=GPIO18，麦克风 SCK/WS 同步改接，SD 保持 GPIO17
 */
static const max98357_pins_t k_audio_pins = {
    .port            = I2S_NUM_0,
    .bclk            = GPIO_NUM_20,
    .lrc             = GPIO_NUM_19,
    .din             = GPIO_NUM_18,    /* 功放 DIN，ESP32 输出 */
    .mic_din         = GPIO_NUM_17,    /* INMP441 SD，ESP32 输入 */
    .sample_rate_hz  = 22050,
    .mclk_multiple   = I2S_MCLK_MULTIPLE_256,
    .bclk_div        = 8,
    .dma_desc_num    = 8,
    .dma_frame_num   = 64,
    .beep_freq_hz    = 440,
    .beep_ms         = 40,
    .task_period_ms  = 500,
};

/**
 * W25Q128 SPI NOR Flash 接线
 *
 * 功能：声明本板外部 Flash 的实际引脚（16 MB，按需求文档 v3.0 第五节）
 * 修改：2026-10-03 新增，作为装配层注入值
 * 修改：2026-10-03 由 W25Q32 改为 W25Q128，结构体类型名同步更名
 */
static const w25q128_pins_t k_flash_pins = {
    .host             = SPI3_HOST,
    .cs               = GPIO_NUM_39,
    .sck              = GPIO_NUM_40,
    .mosi             = GPIO_NUM_41,
    .miso             = GPIO_NUM_42,
    .clk_hz           = 20 * 1000 * 1000,
    .max_transfer_sz  = 4096,
};

/**
 * INMP441 采集参数
 *
 * 功能：麦克风采集参数，由 max98357 的 I2S 全双工控制器提供时钟
 * 修改：2026-10-04 新增
 *
 * @note     采样率与功放一致（22050），保证共用 I2S 时隙对齐；
 *         后续接 ESP-SR 唤醒时如模型要求 16 kHz，需将功放与麦克风
 *         采样率一起改为 16000
 */
static const inmp441_cfg_t k_mic_cfg = {
    .sample_rate_hz = 22050,
    .frame_ms       = 50,
    .channels       = 1,
};

/**
 * INMP441 完整引脚定义
 *
 * 功能：集中描述本板麦克风的实际接线，含共用时钟脚
 * 修改：2026-10-04 新增，从 k_audio_pins 拆出
 *
 * @note     SCK 和 WS 与 MAX98357A 共用（BCLK=20、LRC=19），
 *          物理上接同一根 GPIO，代码里在 k_audio_pins 和 k_mic_pins
 *          两处都写出来是为了让接线一目了然
 */
static const inmp441_pins_t k_mic_pins = {
    .sck         = GPIO_NUM_18,    /* 与 MAX98357A BCLK 共用 */
    .ws          = GPIO_NUM_19,    /* 与 MAX98357A LRC  共用 */
    .sd          = GPIO_NUM_17,    /* 独立数据脚 */
    .vdd         = GPIO_NUM_NC,   /* VCC 直连 3.3V，不经 GPIO */
    .gnd         = GPIO_NUM_NC,   /* GND 直连 */
};

void board_log_pinout(void)
{
    ESP_LOGI(TAG, "--- OLED ---   SCK=%d MOSI=%d CS=%d DC=%d RST=%d",
             (int)k_oled_pins.sck, (int)k_oled_pins.mosi, (int)k_oled_pins.cs,
             (int)k_oled_pins.dc,  (int)k_oled_pins.rst);
    ESP_LOGI(TAG, "--- 功放 ---   BCLK=%d LRC=%d DIN=%d MIC=%d",
             (int)k_audio_pins.bclk, (int)k_audio_pins.lrc,
             (int)k_audio_pins.din, (int)k_audio_pins.mic_din);
    ESP_LOGI(TAG, "--- Flash ---  CS=%d SCK=%d MOSI=%d MISO=%d",
             (int)k_flash_pins.cs, (int)k_flash_pins.sck,
             (int)k_flash_pins.mosi, (int)k_flash_pins.miso);
    ESP_LOGI(TAG, "--- 麦克风 --- SCK=%d WS=%d SD=%d (共用功放时钟)",
             (int)k_mic_pins.sck, (int)k_mic_pins.ws, (int)k_mic_pins.sd);
}

void board_init_devices(void)
{
    /* OLED */
    if (oled_spi_init(&k_oled_pins) != ESP_OK) {
        ESP_LOGE(TAG, "OLED 初始化失败，跳过显示功能");
    }

    /* Flash */
    if (w25q128_init(&k_flash_pins) == ESP_OK) {
        uint8_t jedec[3] = { 0 };
        if (w25q128_read_jedec_id(jedec) != ESP_OK) {
            ESP_LOGW(TAG, "Flash 未检出，暂不提供存储功能");
        }
    } else {
        ESP_LOGE(TAG, "Flash 初始化失败");
    }

    /* 功放 —— 音频链路基础，失败直接中止 */
    /* GAIN(GPIO38) 拉高：MAX98357A 增益使能（逻辑高 = +6 dB，正常输出）；
       不驱动此脚芯片可能处于静音或低功耗状态，喇叭无输出 */
    gpio_config_t gain_cfg = {
        .pin_bit_mask = (1ULL << GPIO_NUM_38),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&gain_cfg));
    gpio_set_level(GPIO_NUM_38, 1);  /* GAIN 拉高 */
    ESP_ERROR_CHECK(max98357_init(&k_audio_pins));
    ESP_ERROR_CHECK(max98357_start_task());

    /* 上电单响一次：确认喇叭链路通（全双工改造后首次验证）。
       自测循环默认关（max98357_set_selftest_beep(false)），避免持续响；
       需要连续自测时手动置 true。 */
    max98357_play_beep();

    /* INMP441 麦克风 —— 复用功放的 I2S 全双工控制器，需在其后初始化 */
    if (inmp441_init(&k_mic_pins, &k_mic_cfg) != ESP_OK) {
        ESP_LOGW(TAG, "麦克风采集启动失败，唤醒/STT 不可用");
    }
}

const max98357_pins_t *board_audio_config(void)
{
    return &k_audio_pins;
}