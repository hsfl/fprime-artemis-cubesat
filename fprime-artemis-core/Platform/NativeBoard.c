/* Software device models beneath the production Zephyr/F Prime drivers. */
#define DT_DRV_COMPAT artemis_lsm6dsox_emul
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/sys/clock.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_stub_device.h>
#include <zephyr/drivers/i2c_emul.h>
#include <zephyr/drivers/adc/adc_emul.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/uart.h>

/* native_sim otherwise starts REALTIME at 1970, which makes Yamcs archive
 * windows misleading. The local supervisor supplies the launch UTC epoch;
 * all subsequent progress still uses the production Zephyr clock. */
static int artemis_native_epoch_init(void) {
    const char *value = getenv("ARTEMIS_TWIN_EPOCH_NS");
    if (value == NULL) return 0;
    char *end;
    errno = 0;
    unsigned long long ns = strtoull(value, &end, 10);
    if (errno || *end || ns < 1577836800000000000ULL || ns > 4102444800000000000ULL)
        return -EINVAL;
    struct timespec epoch = {.tv_sec = ns / 1000000000ULL,
                            .tv_nsec = ns % 1000000000ULL};
    return sys_clock_settime(SYS_CLOCK_REALTIME, &epoch);
}
SYS_INIT(artemis_native_epoch_init, APPLICATION, 0);

struct imu_model { uint8_t registers[256]; uint8_t cursor; };
static struct imu_model model;
static int imu_init(const struct emul *target, const struct device *parent) {
    (void)target; (void)parent;
    memset(&model, 0, sizeof(model));
    model.registers[0x0f] = 0x6c;
    return 0;
}
static int imu_transfer(const struct emul *target, struct i2c_msg *msgs, int count, int addr) {
    (void)target;
    if (addr != 0x6a || count < 1 || count > 2) return -EIO;
    /* Stable 25 C, small gyro reading and approximately +1 g on Z. */
    model.registers[0x22] = 16;
    model.registers[0x2c] = 0;
    model.registers[0x2d] = 32;
    for (int m = 0; m < count; ++m) {
        if (msgs[m].flags & I2C_MSG_READ) {
            for (unsigned n = 0; n < msgs[m].len; ++n)
                msgs[m].buf[n] = model.registers[model.cursor++];
        } else {
            if (!msgs[m].len) return -EINVAL;
            model.cursor = msgs[m].buf[0];
            for (unsigned n = 1; n < msgs[m].len; ++n) {
                uint8_t reg = model.cursor++;
                model.registers[reg] = msgs[m].buf[n];
                if (reg == 0x12 && (msgs[m].buf[n] & 1)) model.registers[reg] = 0;
            }
        }
    }
    return 0;
}
static struct i2c_emul_api imu_api = {.transfer = imu_transfer};
EMUL_DT_INST_DEFINE(0, imu_init, &model, NULL, &imu_api, NULL);
EMUL_STUB_DEVICE(0)

void artemis_native_init(void) {
    const unsigned ids[] = {7, 8, 10, 15, 0, 13, 14};
    for (unsigned i = 0; i < 7; ++i)
        /* TMP36 board divider is 45.3k/10k: ~136 mV at ADC for 25 C. */
        adc_emul_const_value_set(DEVICE_DT_GET(DT_NODELABEL(adc0)), ids[i], 136U + i);
}
void artemis_native_poll_power(void) {
    static int previous = -1;
    int state = gpio_emul_output_get(DEVICE_DT_GET(DT_NODELABEL(gpio0)), 18);
    if (state < 0) return;
    if (state == previous) return;
    previous = state;
    const struct device *uart = DEVICE_DT_GET(DT_ALIAS(power_uart));
    const char *message = state ? "POWER ON\n" : "POWER OFF\n";
    for (; *message; ++message) uart_poll_out(uart, *message);
}
