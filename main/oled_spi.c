/**
 * @file     oled_spi.c
 * @brief    OLED SSD1306 (0.96", SPI) 驱动实现
 *
 * 功能：引脚配置、硬复位、SPI 总线与设备挂载、命令/数据发送
 * 修改：2026-10-03 独立成设备文件；引脚改为由 oled_spi_pins_t 参数注入
 * 修改：2026-10-03 增加 s_ready 幂等保护与参数校验，失败返回错误码而非直接 abort
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"

#include "oled_spi.h"

static const char *TAG = "OLED";

static spi_device_handle_t s_dev  = NULL;
static bool                 s_ready = false;

/**
 * @brief  硬复位：RST 拉低 100 ms -> 拉高 100 ms
 *
 * 功能：让 SSD1306 从任意状态回到已知状态
 * 修改：2026-10-03 改为接收 rst 引脚参数，不再引用全局宏
 */
static void oled_hw_reset(gpio_num_t rst)
{
    gpio_set_level(rst, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(rst, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

/**
 * @brief  配置 DC 与 RST 为推挽输出
 *
 * 功能：初始化控制类引脚，并完成硬复位
 * 修改：2026-10-03 改为接收 dc/rst 参数，返回错误码不再隐式失败
 */
static esp_err_t oled_gpio_init(gpio_num_t dc, gpio_num_t rst)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << dc) | (1ULL << rst),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        return ret;
    }

    gpio_set_level(dc, 0);
    oled_hw_reset(rst);
    return ESP_OK;
}

esp_err_t oled_spi_init(const oled_spi_pins_t *pins)
{
    if (pins == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }

    ESP_ERROR_CHECK(oled_gpio_init(pins->dc, pins->rst));

    spi_bus_config_t buscfg = {
        .miso_io_num     = -1,
        .mosi_io_num     = pins->mosi,
        .sclk_io_num     = pins->sck,
        .max_transfer_sz = pins->max_transfer_sz,
        .flags           = SPICOMMON_BUSFLAG_MASTER,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(pins->host, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = pins->clk_hz,
        .mode           = 0,
        .spics_io_num   = pins->cs,
        .queue_size     = 1,
        .flags          = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(pins->host, &devcfg, &s_dev));

    s_ready = true;
    ESP_LOGI(TAG, "初始化完成 -> SCK=%d MOSI=%d CS=%d DC=%d RST=%d",
             (int)pins->sck, (int)pins->mosi, (int)pins->cs,
             (int)pins->dc,  (int)pins->rst);
    return ESP_OK;
}

spi_device_handle_t oled_spi_handle(void)
{
    return s_dev;
}

esp_err_t oled_spi_write_cmd(uint8_t cmd)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    spi_transaction_t trans = {
        .length    = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_polling_transmit(s_dev, &trans);
}

esp_err_t oled_spi_write_data(const uint8_t *data, size_t len)
{
    if (!s_ready || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    spi_transaction_t trans = {
        .length    = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_dev, &trans);
}

bool oled_spi_is_ready(void)
{
    return s_ready;
}