/**
 * @file     w25q128.c
 * @brief    W25Q128 (16MB SPI NOR Flash) 驱动实现
 *
 * 功能：SPI 总线与设备挂载、JEDEC 识别、裸读写、扇区擦除、忙位轮询
 * 修改：2026-10-03 引脚改为由 w25q128_pins_t 参数注入
 * 修改：2026-10-03 增加参数校验、地址越界检查与幂等保护
 * 修改：2026-10-03 由 W25Q32 更名为 W25Q128，容量 ID 校验 0x16 -> 0x18
 */
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/spi_master.h"

#include "w25q128.h"

static const char *TAG = "W25Q128";

static spi_device_handle_t s_dev   = NULL;
static bool                 s_ready = false;

/* ---- W25Qxx 基本命令 ---- */
#define CMD_WRITE_ENABLE   0x06
#define CMD_READ_STATUS    0x05
#define CMD_READ_DATA      0x03
#define CMD_WRITE_DATA     0x02
#define CMD_SECTOR_ERASE   0x20
#define CMD_JEDEC_ID       0x9F
#define STATUS_BUSY_BIT    0x01

/** 内部操作等待上限，擦除 4KB 扇区最慢约 400ms */
#define OP_WAIT_TIMEOUT_MS 5000

/**
 * @brief  发送 Write Enable 解锁写保护
 *
 * 功能：页编程与擦除的前置命令
 * 修改：2026-10-03 新增，抽成独立函数供 write/erase 复用
 */
static esp_err_t w25q128_write_enable(void)
{
    spi_transaction_t trans = {
        .length    = 8,
        .tx_buffer = (uint8_t[]){ CMD_WRITE_ENABLE },
    };
    return spi_device_transmit(s_dev, &trans);
}

/**
 * @brief  轮询状态寄存器等待内部操作完成
 *
 * 功能：擦除/写入是异步的，需等 BUSY 位清零
 * 修改：2026-10-03 新增，超时上限提至 5 秒以覆盖扇区擦除耗时
 */
static esp_err_t w25q128_wait_ready(void)
{
    uint8_t  status    = 0xFF;
    uint32_t waited_ms = 0;

    while (waited_ms < OP_WAIT_TIMEOUT_MS) {
        uint8_t tx[2] = { CMD_READ_STATUS, 0xFF };
        spi_transaction_t trans = {
            .length    = sizeof(tx) * 8,
            .tx_buffer = tx,
            .rx_buffer = &status,
        };
        esp_err_t ret = spi_device_transmit(s_dev, &trans);
        if (ret != ESP_OK) {
            return ret;
        }
        if ((status & STATUS_BUSY_BIT) == 0) {
            return ESP_OK;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
        waited_ms += 10;
    }

    ESP_LOGE(TAG, "等待内部操作超时");
    return ESP_ERR_TIMEOUT;
}

esp_err_t w25q128_init(const w25q128_pins_t *pins)
{
    if (pins == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ready) {
        return ESP_OK;
    }

    spi_bus_config_t buscfg = {
        .miso_io_num     = pins->miso,
        .mosi_io_num     = pins->mosi,
        .sclk_io_num     = pins->sck,
        .max_transfer_sz = pins->max_transfer_sz,
        .quadhd_io_num   = -1,
        .quadwp_io_num   = -1,
        .flags           = SPICOMMON_BUSFLAG_MASTER,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(pins->host, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = pins->clk_hz,
        .mode           = 0,
        .spics_io_num   = pins->cs,
        .queue_size     = 4,
        .flags          = SPI_DEVICE_NO_DUMMY,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(pins->host, &devcfg, &s_dev));

    s_ready = true;
    ESP_LOGI(TAG, "初始化完成 -> CS=%d SCK=%d MOSI=%d MISO=%d 总容量=%d MB",
             (int)pins->cs, (int)pins->sck, (int)pins->mosi, (int)pins->miso,
             (int)(W25Q128_TOTAL_SIZE / (1024 * 1024)));
    return ESP_OK;
}

esp_err_t w25q128_read_jedec_id(uint8_t *id)
{
    if (!s_ready || id == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t tx[4] = { CMD_JEDEC_ID, 0x00, 0x00, 0x00 };
    spi_transaction_t trans = {
        .length    = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = id,
    };
    esp_err_t ret = spi_device_transmit(s_dev, &trans);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "JEDEC ID = %02X %02X %02X", id[0], id[1], id[2]);

    if (id[0] != W25Q128_VENDOR_ID) {
        ESP_LOGE(TAG, "厂商 ID 应为 %02X，实际 %02X，芯片可能未接好",
                 W25Q128_VENDOR_ID, id[0]);
        return ESP_ERR_NOT_FOUND;
    }
    if (id[1] != W25Q128_CAPACITY_ID) {
        ESP_LOGE(TAG, "容量 ID 应为 %02X（W25Q128），实际 %02X，型号不符",
                 W25Q128_CAPACITY_ID, id[1]);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "型号确认：W25Q128 / 16 MB / 扇区数 %d",
             (int)W25Q128_SECTOR_COUNT);
    return ESP_OK;
}

esp_err_t w25q128_read(uint32_t addr, uint8_t *buf, size_t len)
{
    if (!s_ready || buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((uint64_t)addr + len > W25Q128_TOTAL_SIZE) {
        ESP_LOGE(TAG, "读越界：addr=%u len=%u 超出 %u 字节",
                 (unsigned)addr, (unsigned)len, (unsigned)W25Q128_TOTAL_SIZE);
        return ESP_ERR_INVALID_ARG;
    }

    spi_transaction_t trans = {
        .length    = (4 + len) * 8,
        .rxlength  = len * 8,
        .tx_buffer = (uint8_t[]){ CMD_READ_DATA,
                                  (uint8_t)(addr >> 16),
                                  (uint8_t)(addr >> 8),
                                  (uint8_t)(addr) },
        .rx_buffer = buf,
    };
    return spi_device_transmit(s_dev, &trans);
}

esp_err_t w25q128_write(uint32_t addr, const uint8_t *data, size_t len)
{
    if (!s_ready || data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (len > W25Q128_PAGE_SIZE) {
        ESP_LOGE(TAG, "单次写入 %u 字节超过页大小 %d，跨页需调用方自行拆分",
                 (unsigned)len, (int)W25Q128_PAGE_SIZE);
        return ESP_ERR_INVALID_SIZE;
    }
    if ((uint64_t)addr + len > W25Q128_TOTAL_SIZE) {
        ESP_LOGE(TAG, "写越界：addr=%u len=%u 超出 %u 字节",
                 (unsigned)addr, (unsigned)len, (unsigned)W25Q128_TOTAL_SIZE);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(w25q128_write_enable());

    uint8_t tx[4 + W25Q128_PAGE_SIZE];
    tx[0] = CMD_WRITE_DATA;
    tx[1] = (uint8_t)(addr >> 16);
    tx[2] = (uint8_t)(addr >> 8);
    tx[3] = (uint8_t)(addr);
    memcpy(&tx[4], data, len);

    spi_transaction_t trans = {
        .length    = (4 + len) * 8,
        .tx_buffer = tx,
    };
    esp_err_t ret = spi_device_transmit(s_dev, &trans);
    if (ret != ESP_OK) {
        return ret;
    }
    return w25q128_wait_ready();
}

esp_err_t w25q128_erase_sector(uint32_t addr)
{
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }
    if (addr >= W25Q128_TOTAL_SIZE ||
        (addr % W25Q128_SECTOR_SIZE) != 0) {
        ESP_LOGE(TAG, "扇区地址非法：0x%06X 必须是 4KB 对齐且在 16MB 内",
                 (unsigned)addr);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_ERROR_CHECK(w25q128_write_enable());

    uint8_t erase[4] = { CMD_SECTOR_ERASE,
                         (uint8_t)(addr >> 16),
                         (uint8_t)(addr >> 8),
                         (uint8_t)(addr) };
    spi_transaction_t trans = {
        .length    = sizeof(erase) * 8,
        .tx_buffer = erase,
    };
    esp_err_t ret = spi_device_transmit(s_dev, &trans);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = w25q128_wait_ready();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "扇区 0x%06X 擦除完成", (unsigned)addr);
    }
    return ret;
}

bool w25q128_is_ready(void)
{
    return s_ready;
}