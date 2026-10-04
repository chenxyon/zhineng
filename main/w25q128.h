/**
 * @file     w25q128.h
 * @brief    W25Q128 (16MB SPI NOR Flash) 驱动接口
 *
 * 功能：SPI 总线初始化、JEDEC 识别、裸读写与扇区擦除
 * 修改：2026-10-03 独立成设备文件；引脚改为结构体参数注入
 * 修改：2026-10-03 由 W25Q32 更名为 W25Q128，容量 ID 校验 0x16 -> 0x18，
 *         并新增 32MB 超限时地址溢出校验
 *
 * @note    需求文档（v2.1 第五节）要求 W25Q128：LittleFS 日志分区 10~12MB
 *          + FAT32 导出分区 2~4MB + 预留 1~2MB，总计 15~18MB。
 *
 * @note    全局规则：官方组件 -> d:\components 自定义组件 -> 自建。
 *          d:\components\w25qxx_driver 已提供完整 W25Qxx 驱动（含 FatFs、
 *          磨损均衡、串口 CLI）。本文件只做裸 SPI 层的最小实现；
 *          一旦需要 LittleFS / FAT32 / 分区表 / USB MSC，必须改用 w25qxx_driver
 *          与 fatfs、littlefs 官方组件，不要继续扩本文件。
 */
#ifndef W25Q128_H
#define W25Q128_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 厂商 ID，JEDEC ID 首字节，用于确认芯片在线 */
#define W25Q128_VENDOR_ID     0xEF

/** 容量 ID，W25Q128 应为 0x18（对应 128 Mbit / 16 MB） */
#define W25Q128_CAPACITY_ID    0x18

/** 总容量 16 MB */
#define W25Q128_TOTAL_SIZE     (16 * 1024 * 1024)

/** 页编程大小 256 B，跨页写入需自行拆分 */
#define W25Q128_PAGE_SIZE      256

/** 扇区大小 4 KB，为最小擦除单位 */
#define W25Q128_SECTOR_SIZE    4096

/** 块大小 64 KB */
#define W25Q128_BLOCK_SIZE     (64 * 1024)

/** 芯片总扇区数 4096 个 */
#define W25Q128_SECTOR_COUNT   (W25Q128_TOTAL_SIZE / W25Q128_SECTOR_SIZE)

/**
 * W25Q128 硬件配置
 *
 * 功能：集中描述本设备所需引脚与总线参数，由 main.c 构造后注入 init
 * 修改：2026-10-03 新增，取代原先散落在 .c 里的 W25Q32_PIN_* 宏
 * 修改：2026-10-03 文件更名为 W25Q128
 */
typedef struct {
    spi_host_device_t host;    /*!< SPI 控制器 */
    gpio_num_t cs;             /*!< 片选脚 */
    gpio_num_t sck;            /*!< 时钟脚 */
    gpio_num_t mosi;           /*!< 数据输出脚 */
    gpio_num_t miso;           /*!< 数据输入脚 */
    int clk_hz;                /*!< SPI 时钟频率 */
    int max_transfer_sz;       /*!< 单次传输最大字节数 */
} w25q128_pins_t;

/**
 * @brief  初始化 SPI 总线并挂载 W25Q128
 *
 * 功能：完成本设备全部引脚初始化，并打印实际生效的引脚
 * 修改：2026-10-03 改为接收 w25q128_pins_t 参数，引脚不再硬编码
 *
 * @param pins  引脚与总线配置，不可为 NULL
 */
esp_err_t w25q128_init(const w25q128_pins_t *pins);

/**
 * @brief  读取 JEDEC ID 并校验型号
 *
 * 功能：确认芯片在线与容量是否符合预期（厂商 0xEF / 容量 0x18）
 * 修改：2026-10-03 新增
 *
 * @param id  输出缓冲区，长度 >= 3（厂商 / 容量 / 型号）
 */
esp_err_t w25q128_read_jedec_id(uint8_t *id);

/**
 * @brief  读 Flash 数据
 *
 * 功能：裸 SPI 读（0x03），含地址越界检查
 * 修改：2026-10-03 新增
 */
esp_err_t w25q128_read(uint32_t addr, uint8_t *buf, size_t len);

/**
 * @brief  写 Flash 数据
 *
 * 功能：自动发送 Write Enable 后执行页编程（0x02）
 * 修改：2026-10-03 单次上限由 256 提升为受 W25Q128_PAGE_SIZE 约束，
 *         并加入地址越界检查；跨页写入仍需调用方自行拆分
 */
esp_err_t w25q128_write(uint32_t addr, const uint8_t *data, size_t len);

/**
 * @brief  擦除一个 4 KB 扇区
 *
 * 功能：扇区擦除（0x20）
 * 修改：2026-10-03 新增
 */
esp_err_t w25q128_erase_sector(uint32_t addr);

/**
 * @brief  查询 Flash 是否初始化成功
 *
 * 功能：状态查询
 * 修改：2026-10-03 新增
 */
bool w25q128_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* W25Q128_H */