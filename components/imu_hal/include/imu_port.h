/*
 * I2C transport for the IMU: the platform port the MPU-6050 driver talks
 * through, plus the geometry of the installed bus.
 *
 * Only the hooks and the bus state live here. The wiring diagnostics and the
 * header-pin search are in imu_bus.c, which reads this state through the
 * accessors at the bottom of this header.
 */
#ifndef IMU_PORT_H
#define IMU_PORT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t imu_port_init(int port, int sda_gpio, int scl_gpio, uint32_t hz, uint8_t addr_8bit);

/* Move the installed bus to another pin pair (used after a successful search). */
esp_err_t imu_port_reinit(int sda, int scl, uint32_t hz, uint8_t addr_8bit);

int32_t imu_port_write(void *handle, uint8_t reg, const uint8_t *bufp, uint16_t len);
int32_t imu_port_read(void *handle, uint8_t reg, uint8_t *bufp, uint16_t len);
void    imu_port_mdelay(uint32_t ms);

uint32_t imu_port_error_count(void);

/* --- geometry of the installed bus (read-only, for the diagnostics) --- */
bool imu_port_ready(void);
int  imu_port_bus(void);       /* I2C port number in use                         */
int  imu_port_sda_gpio(void);
int  imu_port_scl_gpio(void);

#ifdef __cplusplus
}
#endif

#endif /* IMU_PORT_H */
