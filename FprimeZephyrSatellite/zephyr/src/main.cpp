#include "SatelliteController/ControllerApp.hpp"

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main() {
    if (!SatelliteController::controllerAppInitialize()) {
        printk("satellite controller initialization failed; radio remains safe-off\n");
    }
    while (true) {
        SatelliteController::controllerAppPoll(k_uptime_get_32());
        k_sleep(K_MSEC(1));
    }
    return 0;
}
