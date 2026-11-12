// ======================================================================
// \title  Main.cpp
// \brief main program for the F' application. Intended for CLI-based systems (Linux, macOS)
//
// ======================================================================
// Used to access topology functions
#include <FprimeArtemisCore/Deployments/PayloadComputerDeployment/Top/PayloadComputerDeploymentTopology.hpp>
// OSAL initialization
#include <Os/Os.hpp>
// Used for signal handling shutdown
#include <signal.h>
// Used for command line argument processing
#include <getopt.h>
// Used for atoi
#include <cstdlib>
#include <cstring>
#include <fstream>
// Used for logging to the console
#include <Fw/Logger/Logger.hpp>

/**
 * \brief print command line help message
 *
 * This will print a command line help message including the available command line arguments.
 *
 * @param app: name of application
 */
void print_usage(const char* app) {
    Fw::Logger::log("Usage: ./%s [options]\n-b\tBaud rate\n-d\tUART Device\n", app);
}

/**
 * \brief shutdown topology cycling on signal
 *
 * The reference topology allows for a simulated cycling of the rate groups. This simulated cycling needs to be stopped
 * in order for the program to shutdown. This is done via handling signals such that it is performed via Ctrl-C
 *
 * @param signum
 */
static void signalHandler(int signum) {
    PayloadComputerDeployment::stopRateGroups();
}

/**
 * \brief execute the program
 *
 * This F´ program is designed to run in standard environments (e.g. Linux/macOs running on a laptop). Thus it uses
 * command line inputs to specify how to connect.
 *
 * @param argc: argument count supplied to program
 * @param argv: argument values supplied to program
 * @return: 0 on success, something else on failure
 */
int main(int argc, char* argv[]) {
    I32 option = 0;
    CHAR* uart_device = nullptr;
    U32 baud_rate = 115200;

    const bool twin = std::getenv("ARTEMIS_TWIN") != nullptr;
    if (twin) {
        const char* backend = std::getenv("LEPTON_CAMERA_BACKEND");
        const char* sample = std::getenv("C3M_LEPTON_SAMPLE_CSV");
        if (!backend || std::strcmp(backend, "sample") != 0 || !sample ||
            !std::ifstream(sample).good()) {
            Fw::Logger::log("Twin requires LEPTON_CAMERA_BACKEND=sample and a readable C3M_LEPTON_SAMPLE_CSV\n");
            return 2;
        }
    }

    Os::init();

    // Loop while reading the getopt supplied options
    while ((option = getopt(argc, argv, "hb:d:")) != -1) {
        switch (option) {
            // Handle the -b baud rate argument
            case 'b':
                baud_rate = static_cast<U32>(atoi(optarg));
                break;
            // Handle the -d device argument
            case 'd':
                uart_device = optarg;
                break;
            // Cascade intended: help output
            case 'h':
            // Cascade intended: help output
            case '?':
            // Default case: output help and exit
            default:
                print_usage(argv[0]);
                return (option == 'h') ? 0 : 1;
        }
    }
    // Object for communicating state to the topology
    if (twin && uart_device == nullptr) {
        Fw::Logger::log("Twin requires an explicit FC UART endpoint (-d)\n");
        return 2;
    }
    PayloadComputerDeployment::TopologyState inputs{};
    inputs.uartDevice = uart_device;
    inputs.baudRate = baud_rate;

    // Setup program shutdown via Ctrl-C
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    Fw::Logger::log("Hit Ctrl-C to quit\n");

    // Setup, cycle, and teardown topology
    PayloadComputerDeployment::setupTopology(inputs);
    PayloadComputerDeployment::startRateGroups();
    PayloadComputerDeployment::teardownTopology(inputs);
    Fw::Logger::log("Exiting...\n");
    return 0;
}
