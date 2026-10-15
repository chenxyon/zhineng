/**
 * @file     main.c
 * @brief    ESP32-S3 智能语音终端 —— 板级装配入口
 *
 * 功能：调用板级初始化，启动配网；不含引脚定义与外设细节
 * 修改：2026-10-03 由 zhineng.c 重命名为 main.c，统一入口命名
 * 修改：2026-10-03 拆分为「板级装配层」+「每设备独立文件」
 * 修改：2026-10-03 新增 WiFi 配网接入（需求文档第四章）
 * 修改：2026-10-03 把引脚定义与设备初始化全面下沉到 board.c，
 *       app_main() 仅做串行调用
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "max98357.h"
#include "app_wifi.h"
#include "app_voice.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-S3 智能语音终端 ===");

    board_log_pinout();     /* 1. 打印接线总表 */
    board_init_devices();   /* 2. 初始化 OLED / Flash / 功放 + TTS 输出槽 */
    /* 3. 本项目有功放才订阅播报：无功放项目直接调 app_wifi_init() 即可 */
    app_wifi_set_notify_cb(app_voice_on_wifi_event, NULL);
    app_wifi_init();        /* 4. 启动 WiFi 配网 */

    ESP_LOGI(TAG, "初始化完成 —— 等待 WiFi 连接与语音唤醒");

    /* 调试：强制验证喇叭链路。能听见哔-长音说明 I2S 通，
     * 听不见回头查 GAIN38/接线/供电。调通后删掉这段即可。 */
    vTaskDelay(pdMS_TO_TICKS(1000));
    max98357_play_beep();
    vTaskDelay(pdMS_TO_TICKS(200));
    max98357_play_tone(880, 500);
}
