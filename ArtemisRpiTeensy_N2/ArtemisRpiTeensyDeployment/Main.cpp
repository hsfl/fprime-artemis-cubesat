// ======================================================================
// \title  Main.cpp
// \brief main program for the F' application. Intended for CLI-based systems (Linux, macOS)
//
// ======================================================================
// Used to access topology functions
#include <ArtemisRpiTeensyDeployment/Top/ArtemisRpiTeensyDeploymentTopology.hpp>
// OSAL initialization
#include <Os/Os.hpp>
// Used for signal handling shutdown
#include <signal.h>
// Used for command line argument processing
#include <getopt.h>
// Used for printf functions
#include <cstdlib>
#include <cerrno>
#include <climits>
#include <cstdio>

/**
 * \brief print command line help message
 *
 * This will print a command line help message including the available command line arguments.
 *
 * @param app: name of application
 */
void print_usage(const char* app) {
    (void)printf("Usage: ./%s [options]\n-d\tUART device path (default: /dev/serial0)\n", app);
#ifdef NEUTRON2_CDR_TCP
    (void)printf("-a\tTCP listen IPv4 address (default: 127.0.0.1)\n-p\tTCP listen port (default: 50000)\n");
#endif
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
    ArtemisRpiTeensyDeployment::stopRateGroups();
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
    const CHAR* uartDevice = "/dev/serial0";
#ifdef NEUTRON2_CDR_TCP
    const CHAR* tcpListenAddress = "127.0.0.1";
    U16 tcpListenPort = 50000;
#endif

    Os::init();

    // Loop while reading the getopt supplied options
#ifdef NEUTRON2_CDR_TCP
    while ((option = getopt(argc, argv, "hd:a:p:")) != -1) {
#else
    while ((option = getopt(argc, argv, "hd:")) != -1) {
#endif
        switch (option) {
            // Handle the -d argument for UART device path
            case 'd':
                uartDevice = optarg;
                break;
#ifdef NEUTRON2_CDR_TCP
            case 'a':
                tcpListenAddress = optarg;
                break;
            case 'p': {
                char* end = nullptr;
                errno = 0;
                const unsigned long parsed = std::strtoul(optarg, &end, 10);
                if ((errno != 0) || (end == optarg) || (*end != '\0') || (parsed == 0) || (parsed > USHRT_MAX)) {
                    (void)fprintf(stderr, "Invalid TCP listen port: %s\n", optarg);
                    print_usage(argv[0]);
                    return 2;
                }
                tcpListenPort = static_cast<U16>(parsed);
                break;
            }
#endif
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
    // TopologyState can be large with subtopology state; keep it off thread stack.
    static ArtemisRpiTeensyDeployment::TopologyState inputs;
    inputs.uartDevice = uartDevice;
#ifdef NEUTRON2_CDR_TCP
    inputs.tcpListenAddress = tcpListenAddress;
    inputs.tcpListenPort = tcpListenPort;
#endif

    // Setup program shutdown via Ctrl-C
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
#if defined(NEUTRON2_CDR_TCP) && defined(SIGPIPE)
    // Let the TCP driver handle a closed peer through its send error path.
    signal(SIGPIPE, SIG_IGN);
#endif
    (void)printf("Hit Ctrl-C to quit\n");

    // Setup, cycle, and teardown topology
    ArtemisRpiTeensyDeployment::setupTopology(inputs);
    ArtemisRpiTeensyDeployment::startRateGroups(Fw::TimeInterval(1,0));  // Program loop cycling rate groups at 1Hz
    ArtemisRpiTeensyDeployment::teardownTopology(inputs);
    (void)printf("Exiting...\n");
    return 0;
}
