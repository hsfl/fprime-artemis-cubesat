// ======================================================================
// \title  Main.cpp
// \brief main program for the F' application. Intended for Zephyr (Teensy 4.1)
//
// ======================================================================
// Used to access topology functions
#include <FprimeArtemisCore/Deployments/FlightControllerDeployment/Top/FlightControllerDeploymentTopology.hpp>
// OSAL initialization
#include <Os/Os.hpp>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

// UART used for the ground link. Exposed over USB CDC ACM on the Teensy 4.1.
const struct device* serial = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

int main(int argc, char* argv[]) {
    // ** DO NOT REMOVE **//
    //
    // This sleep is necessary to allow the USB CDC ACM interface to initialize before
    // the application starts writing to it.
    k_sleep(K_MSEC(3000));

    Os::init();

    // Object for communicating state to the topology
    FprimeArtemisCore::TopologyState inputs;
    inputs.uartDevice = serial;
    inputs.baudRate = 115200;

    // Setup, cycle, and teardown topology
    FprimeArtemisCore::setupTopology(inputs);
    FprimeArtemisCore::startRateGroups();  // Program loop
    FprimeArtemisCore::teardownTopology(inputs);
    return 0;
}
