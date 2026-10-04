/**
 * @file     oled_spi.h
 * @brief    OLED SSD1306 (0.96", SPI) 驱动接口
 *
 * 功能：OLED 硬件初始化、命令/数据发送、状态查询
 * 修改：2026-10-03 独立成设备文件；引脚由宏改为结构体参数注入，驱动可跨板复用
 */
#ifndef OLED_SPI_H
#define OLED_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * OLED 硬件配置
 *
 * 功能：集中描述本设备所需引脚与总线参数，由 main.c 构造后注入 init
 * 修改：2026-10-03 新增，取代原先散落在 .c 里的 OLED_PIN_* 宏
 */
typedef struct {
    spi_host_device_t host;        /*!< 使用的 SPI 控制器 */
    gpio_num_t sck;                /*!< 时钟脚 */
    gpio_num_t mosi;               /*!< 数据输出脚 */
    gpio_num_t cs;                 /*!< 片选脚 */
    gpio_num_t dc;                 /*!< 数据/命令选择脚（strap 脚） */
    gpio_num_t rst;                /*!< 复位脚（strap 脚） */
    int clk_hz;                    /*!< SPI 时钟频率 */
    int max_transfer_sz;           /*!< 单次传输最大字节数 */
} oled_spi_pins_t;

/**
 * @brief  初始化 OLED：配置 DC/RST 引脚、硬复位、挂载 SPI 设备
 *
 * 功能：完成本设备全部引脚初始化，并在成功时打印实际生效的引脚
 * 修改：2026-10-03 改为接收 oled_spi_pins_t 参数，引脚不再硬编码在驱动内
 *
 * @param pins  引脚与总线配置，不可为 NULL
 * @return ESP_OK 成功；参数非法或已初始化返回对应错误码
 */
esp_err_t oled_spi_init(const oled_spi_pins_t *pins);

/**
 * @brief  取已挂载的 SPI 设备句柄
 *
 * 功能：供上层直接复用总线句柄发数据
 * 修改：2026-10-03 新增
 */
spi_device_handle_t oled_spi_handle(void);

/**
 * @brief  发送单字节命令（DC 拉低）
 *
 * 功能：向 SSD1306 下发控制命令
 * 修改：2026-10-03 新增
 */
esp_err_t oled_spi_write_cmd(uint8_t cmd);

/**
 * @brief  发送显示数据（DC 拉高）
 *
 * 功能：向 SSD1306 写入像素数据
 * 修改：2026-10-03 新增
 */
esp_err_t oled_spi_write_data(const uint8_t *data, size_t len);

/**
 * @brief  查询 OLED 是否初始化成功
 *
 * 功能：状态查询
 * 修改：2026-10-03 新增
 */
bool oled_spi_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* OLED_SPI_H */