/* IDF5.4 新驱动适配：保持 IMU API、地址、50ms超时和物理接线，不改变检测逻辑。 */
#include "imu_port.h"
#include <string.h>
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static i2c_master_bus_handle_t bus;
static i2c_master_dev_handle_t device;
static int port_number, sda_pin, scl_pin;
static uint32_t errors;
#define TIMEOUT_MS 50

esp_err_t imu_port_init(int port, int sda, int scl, uint32_t hz, uint8_t addr8) {
    if (bus) return ESP_ERR_INVALID_STATE;
    /* I2C0 已由 ES8388/XL9555 占用，配置不得误用它。 */
    if (port != 1 || sda == 41 || sda == 42 || scl == 41 || scl == 42)
        return ESP_ERR_INVALID_ARG;
    i2c_master_bus_config_t cfg = {
        .i2c_port = port, .sda_io_num = sda, .scl_io_num = scl,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&cfg, &bus);
    if (err != ESP_OK) return err;
    i2c_device_config_t dev = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = addr8 >> 1,
        .scl_speed_hz = hz,
    };
    err = i2c_master_bus_add_device(bus, &dev, &device);
    if (err != ESP_OK) { i2c_del_master_bus(bus); bus = NULL; return err; }
    port_number = port; sda_pin = sda; scl_pin = scl;
    ESP_LOGI("imu_port", "I2C%d SDA=IO%d SCL=IO%d %luHz addr=0x%02X (new driver)",
             port, sda, scl, (unsigned long)hz, addr8 >> 1);
    return ESP_OK;
}
esp_err_t imu_port_reinit(int sda, int scl, uint32_t hz, uint8_t addr8) {
    if (device) { esp_err_t e = i2c_master_bus_rm_device(device); if (e != ESP_OK) return e; device = NULL; }
    if (bus) { esp_err_t e = i2c_del_master_bus(bus); if (e != ESP_OK) return e; bus = NULL; }
    return imu_port_init(port_number, sda, scl, hz, addr8);
}
int32_t imu_port_write(void *handle, uint8_t reg, const uint8_t *data, uint16_t len) {
    (void)handle;
    if (!device || !data || !len || len > 64) { errors++; return -1; }
    uint8_t out[65]; out[0] = reg; memcpy(out + 1, data, len);
    if (i2c_master_transmit(device, out, len + 1, TIMEOUT_MS) != ESP_OK) { errors++; return -1; }
    return 0;
}
int32_t imu_port_read(void *handle, uint8_t reg, uint8_t *data, uint16_t len) {
    (void)handle;
    if (!device || !data || !len) { errors++; return -1; }
    if (i2c_master_transmit_receive(device, &reg, 1, data, len, TIMEOUT_MS) != ESP_OK) { errors++; return -1; }
    return 0;
}
void imu_port_mdelay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms ? ms : 1)); }
uint32_t imu_port_error_count(void) { return errors; }
bool imu_port_ready(void) { return device != NULL; }
int imu_port_bus(void) { return port_number; }
int imu_port_sda_gpio(void) { return sda_pin; }
int imu_port_scl_gpio(void) { return scl_pin; }
