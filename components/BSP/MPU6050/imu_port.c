#include "imu_port.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "imu_port";

#define IMU_I2C_TIMEOUT_MS 50
#define IMU_WRITE_MAX      64

static i2c_port_t s_port;
static uint8_t    s_addr7;
static bool       s_ready;
static uint32_t   s_errors;
static int        s_sda_gpio;
static int        s_scl_gpio;

esp_err_t imu_port_init(int port, int sda_gpio, int scl_gpio, uint32_t hz, uint8_t addr_8bit)
{
    s_port = (i2c_port_t)port;
    s_addr7 = (uint8_t)(addr_8bit >> 1);
    s_sda_gpio = sda_gpio;
    s_scl_gpio = scl_gpio;

    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = hz,
        .clk_flags = 0,
    };

    esp_err_t err = i2c_param_config(s_port, &cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = i2c_driver_install(s_port, I2C_MODE_MASTER, 0, 0, 0);
    if ((err != ESP_OK) && (err != ESP_ERR_INVALID_STATE)) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    ESP_LOGI(TAG, "I2C%d installed: SDA=IO%d SCL=IO%d %u Hz, device address 0x%02X (7-bit 0x%02X)",
             port, sda_gpio, scl_gpio, (unsigned)hz, (unsigned)addr_8bit, (unsigned)s_addr7);
    return ESP_OK;
}

esp_err_t imu_port_reinit(int sda, int scl, uint32_t hz, uint8_t addr_8bit)
{
    const int old_sda = s_sda_gpio;
    const int old_scl = s_scl_gpio;

    if (s_ready) {
        (void)i2c_driver_delete(s_port);
        s_ready = false;
    }

    /* Release the old pins from the peripheral before they are probed again. */
    const gpio_config_t release = {
        .pin_bit_mask = (1ULL << (uint32_t)old_sda) | (1ULL << (uint32_t)old_scl),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&release);

    return imu_port_init((int)s_port, sda, scl, hz, addr_8bit);
}

int32_t imu_port_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len)
{
    (void)handle;
    if (!s_ready || (bufp == NULL) || (len == 0) || (len > IMU_WRITE_MAX)) {
        s_errors++;
        return -1;
    }

    uint8_t buf[IMU_WRITE_MAX + 1];
    buf[0] = reg;
    memcpy(&buf[1], bufp, len);

    const esp_err_t err = i2c_master_write_to_device(s_port, s_addr7, buf, (size_t)len + 1,
                                                     pdMS_TO_TICKS(IMU_I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        s_errors++;
        return -1;
    }
    return 0;
}

int32_t imu_port_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len)
{
    (void)handle;
    if (!s_ready || (bufp == NULL) || (len == 0)) {
        s_errors++;
        return -1;
    }

    const esp_err_t err = i2c_master_write_read_device(s_port, s_addr7, &reg, 1, bufp, len,
                                                       pdMS_TO_TICKS(IMU_I2C_TIMEOUT_MS));
    if (err != ESP_OK) {
        s_errors++;
        return -1;
    }
    return 0;
}

void imu_port_mdelay(uint32_t ms)
{
    vTaskDelay(pdMS_TO_TICKS(ms > 0 ? ms : 1));
}

uint32_t imu_port_error_count(void)
{
    return s_errors;
}

bool imu_port_ready(void)
{
    return s_ready;
}

int imu_port_bus(void)
{
    return (int)s_port;
}

int imu_port_sda_gpio(void)
{
    return s_sda_gpio;
}

int imu_port_scl_gpio(void)
{
    return s_scl_gpio;
}
