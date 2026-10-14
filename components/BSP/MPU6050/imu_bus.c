#include "imu_bus.h"

#include <stdio.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "imu_port.h"

static const char *TAG = "imu_port";

/* Addresses worth naming when they answer, so a scan is self-explanatory. */
static const char *known_device(uint8_t addr7)
{
    switch (addr7) {
    case 0x10: return "ES8388 audio codec (onboard)";
    case 0x12:
    case 0x13: return "QMA6100P accelerometer (onboard, NOT a gyroscope)";
    case 0x1E: return "AP3216C light/proximity (onboard)";
    case 0x20:
    case 0x21:
    case 0x22:
    case 0x23: return "XL9555 IO expander (onboard)";
    case 0x50:
    case 0x51: return "24C02 EEPROM (onboard)";
    case 0x68:
    case 0x69: return "MPU-6050 gyroscope+accelerometer";
    default:   return NULL;
    }
}

static void pin_as_input(gpio_num_t pin, bool pull_down)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << (uint32_t)pin,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    (void)gpio_config(&cfg);
}

void imu_bus_log_idle_levels(void)
{
    if (!imu_port_ready()) {
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    const int sda = gpio_get_level((gpio_num_t)imu_port_sda_gpio());
    const int scl = gpio_get_level((gpio_num_t)imu_port_scl_gpio());
    ESP_LOGI(TAG, "idle levels: SDA(IO%d)=%d SCL(IO%d)=%d (both must read 1 with pull-ups)",
             imu_port_sda_gpio(), sda, imu_port_scl_gpio(), scl);
    if ((sda == 0) || (scl == 0)) {
        ESP_LOGE(TAG, "a bus line is held low: check for a short, swapped SDA/SCL, or a module"
                      " that is wired to these pins but powered from the wrong rail");
    }
}

/* Probe one bus and log every address that acknowledges. Returns how many. */
static int scan_port(i2c_port_t port)
{
    int found = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (uint8_t)((addr << 1) | I2C_MASTER_WRITE), true);
        i2c_master_stop(cmd);
        const esp_err_t err = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(20));
        i2c_cmd_link_delete(cmd);

        if (err == ESP_OK) {
            const char *name = known_device(addr);
            found++;
            if (name != NULL) {
                ESP_LOGI(TAG, "  ACK 7-bit 0x%02X (8-bit 0x%02X)  <- %s",
                         (unsigned)addr, (unsigned)(addr << 1), name);
            }
            else {
                ESP_LOGI(TAG, "  ACK 7-bit 0x%02X (8-bit 0x%02X)",
                         (unsigned)addr, (unsigned)(addr << 1));
            }
        }
    }
    return found;
}

void imu_bus_scan(void)
{
    if (!imu_port_ready()) {
        ESP_LOGW(TAG, "bus not installed, cannot scan");
        return;
    }
    ESP_LOGI(TAG, "scanning I2C%d (SDA=IO%d SCL=IO%d) ...", imu_port_bus(),
             imu_port_sda_gpio(), imu_port_scl_gpio());
    const int found = scan_port((i2c_port_t)imu_port_bus());
    ESP_LOGI(TAG, "scan done: %d device(s) answered", found);
}

/*
 * An address ACK alone is not proof of a working bus: on a mis-wired pair the
 * ACK can be spurious. Read the identity register and require a known value.
 */
static uint8_t find_imu_address(i2c_port_t port)
{
    static const struct {
        uint8_t addr7;
        uint8_t reg;
        uint8_t expect;
        uint8_t mask;
        const char *name;
    } probes[] = {
        { 0x68, 0x75, 0x68, 0x7E, "MPU-6050 gyro+accel" },
        { 0x69, 0x75, 0x68, 0x7E, "MPU-6050 gyro+accel (AD0 high)" },
    };

    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        uint8_t id = 0;
        const esp_err_t err = i2c_master_write_read_device(port, probes[i].addr7,
                                                           &probes[i].reg, 1, &id, 1,
                                                           pdMS_TO_TICKS(30));
        if (err != ESP_OK) {
            continue;
        }
        if ((id & probes[i].mask) == (probes[i].expect & probes[i].mask)) {
            ESP_LOGW(TAG, "  validated %s at 7-bit 0x%02X (ID=0x%02X)",
                     probes[i].name, (unsigned)probes[i].addr7, (unsigned)id);
            return probes[i].addr7;
        }
        ESP_LOGI(TAG, "  0x%02X answered but ID=0x%02X matches no known part",
                 (unsigned)probes[i].addr7, (unsigned)id);
    }
    return 0;
}

/*
 * Candidate header GPIOs for the wiring search.
 *
 * Excluded on purpose:
 *   IO0            BOOT strap / XL9555 INT / 1-wire (manual: not a normal user pin)
 *   IO19, IO20     native USB D-/D+
 *   IO35..IO37     manual marks these "do not use"
 *
 * IO41/IO42 are included on purpose: they are the board's own I2C0 (SDA/SCL in
 * the manual's pin table) and therefore the most likely place to plug an
 * external IMU. The search only runs when the configured pins answered nothing,
 * and it probes on the *other* I2C peripheral, so the onboard devices sharing
 * IO41/IO42 (ES8388, AP3216C, 24C02, XL9555) are not disturbed by it.
 */
static const int k_candidate_pins[] = {
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 21,
    38, 39, 40, 41, 42, 45, 46, 47, 48
};

#define CANDIDATE_COUNT ((int)(sizeof(k_candidate_pins) / sizeof(k_candidate_pins[0])))
#define MAX_PULLUP_PINS 16

/* Pins that stay high against an internal pull-down carry an external pull-up -
   the signature of an attached I2C module (or of onboard pull-ups). */
static int find_pullup_pins(int *pins, int max_pins)
{
    int n = 0;
    for (int i = 0; i < CANDIDATE_COUNT; i++) {
        const gpio_num_t pin = (gpio_num_t)k_candidate_pins[i];
        pin_as_input(pin, true);
        vTaskDelay(pdMS_TO_TICKS(2));
        if (gpio_get_level(pin) != 0) {
            if (n < max_pins) {
                pins[n] = k_candidate_pins[i];
            }
            n++;
        }
        pin_as_input(pin, false);
    }
    return (n > max_pins) ? max_pins : n;
}

/*
 * Install a temporary 100 kHz master, validate the pair, then release it. The
 * probe never uses the port the application is already running on.
 */
static bool try_pair(int sda, int scl, uint8_t *addr7)
{
    const i2c_port_t probe_port = (imu_port_bus() == (int)I2C_NUM_1) ? I2C_NUM_0 : I2C_NUM_1;

    const i2c_config_t cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = sda,
        .scl_io_num = scl,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000, /* slower: tolerant of long jumper wires */
        .clk_flags = 0,
    };

    if (i2c_param_config(probe_port, &cfg) != ESP_OK) {
        return false;
    }
    if (i2c_driver_install(probe_port, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
        return false;
    }

    const uint8_t found = find_imu_address(probe_port);
    (void)i2c_driver_delete(probe_port);

    if (found != 0) {
        ESP_LOGW(TAG, "  -> IMU confirmed on SDA=IO%d SCL=IO%d at 0x%02X", sda, scl, (unsigned)found);
        if (addr7 != NULL) {
            *addr7 = found;
        }
        return true;
    }
    return false;
}

bool imu_bus_find_working_pair(int *sda, int *scl, uint8_t *addr7)
{
    int pins[MAX_PULLUP_PINS];
    const int n = find_pullup_pins(pins, MAX_PULLUP_PINS);

    if (n == 0) {
        ESP_LOGW(TAG, "no GPIO held high against a pull-down: the module is most likely"
                      " unpowered or not connected at all");
        return false;
    }

    char list[112];
    int used = 0;
    list[0] = '\0';
    for (int i = 0; i < n; i++) {
        const int written = snprintf(&list[used], sizeof(list) - (size_t)used, "%sIO%d",
                                     (i == 0) ? "" : " ", pins[i]);
        if (written <= 0) {
            break;
        }
        used += written;
        if ((size_t)used >= sizeof(list)) {
            break;
        }
    }
    ESP_LOGW(TAG, "%d pin(s) carry an external pull-up: %s", n, list);
    ESP_LOGW(TAG, "trying each pair in both orders and validating the identity register ...");

    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            if (try_pair(pins[i], pins[j], addr7)) {
                if (sda != NULL) {
                    *sda = pins[i];
                }
                if (scl != NULL) {
                    *scl = pins[j];
                }
                return true;
            }
            if (try_pair(pins[j], pins[i], addr7)) {
                if (sda != NULL) {
                    *sda = pins[j];
                }
                if (scl != NULL) {
                    *scl = pins[i];
                }
                return true;
            }
        }
    }

    ESP_LOGW(TAG, "no IMU was confirmed on any probed pair. Check the module's power LED,"
                  " re-seat the jumpers, and confirm the module really is an MPU-6050"
                  " part");
    return false;
}
