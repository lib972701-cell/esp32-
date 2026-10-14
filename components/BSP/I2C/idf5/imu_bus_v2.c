/* 集成版诊断只访问配置好的 IMU 总线，禁止自动改动音频、DTU等在用引脚。 */
#include "imu_bus.h"
#include "imu_port.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_log.h"
void imu_bus_log_idle_levels(void) {
    if (!imu_port_ready()) return;
    ESP_LOGI("imu_port", "idle: SDA(IO%d)=%d SCL(IO%d)=%d", imu_port_sda_gpio(),
             gpio_get_level(imu_port_sda_gpio()), imu_port_scl_gpio(), gpio_get_level(imu_port_scl_gpio()));
}
void imu_bus_scan(void) {
    i2c_master_bus_handle_t bus = NULL;
    if (!imu_port_ready() || i2c_master_get_bus_handle(imu_port_bus(), &bus) != ESP_OK) return;
    for (unsigned addr = 8; addr <= 0x77; addr++)
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) ESP_LOGI("imu_port", "ACK 0x%02X", addr);
}
bool imu_bus_find_working_pair(int *sda, int *scl, uint8_t *addr7) {
    (void)sda; (void)scl; (void)addr7;
    ESP_LOGW("imu_port", "Integrated firmware only uses configured IMU pins; automatic rewiring disabled");
    return false;
}
