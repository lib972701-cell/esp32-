/*
 * I2C bus diagnostics and discovery.
 *
 * The board's main I2C0 (IO41/IO42) is shared with the onboard codec, light
 * sensor, accelerometer, IO expander and EEPROM, so a scan there is the first
 * check. When the module does not answer, the firmware searches the header pins:
 * it probes candidate GPIOs for an external pull-up (a GY-521 carries 4.7k
 * pull-ups to its own 3.3 V rail), then tries every pair in both orders and
 * accepts a pair only when a real identity register read matches a known part.
 * No pin is assumed, and a bare address ACK is not trusted.
 */
#ifndef IMU_BUS_H
#define IMU_BUS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Log the idle level of SDA and SCL: a line stuck low means a wiring fault. */
void imu_bus_log_idle_levels(void);

/* Scan every 7-bit address on the installed bus and log what answers. */
void imu_bus_scan(void);

/*
 * Search the header pins for a working IMU bus.
 * On success returns true and fills the pin pair plus the 7-bit address found.
 */
bool imu_bus_find_working_pair(int *sda, int *scl, uint8_t *addr7);

#ifdef __cplusplus
}
#endif

#endif /* IMU_BUS_H */
