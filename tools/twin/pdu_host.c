/* Native physical-I/O adapter for the actual PDU application/protocol sources.
 * GPIO levels are modeled; UART stdin/stdout and scheduler ticks are real host
 * interfaces. No physical device API is called by this executable. */
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "app.h"
#include "definitions.h"
#include "FreeRTOS.h"
#include "pdu_packet.h"

static bool pins[PIN_COUNT];
static uint8_t input[2048];
static size_t input_size, input_position;
static struct timespec started;
TEST_RSTC_REGS test_rstc_regs = {0U};

static uint64_t monotonic_ms(void) {
    struct timespec now;
    (void)clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * 1000U + (uint64_t)now.tv_nsec / 1000000U;
}
void mock_pin_write(uint32_t pin, bool value) { assert(pin < PIN_COUNT); pins[pin] = value; }
bool PORT_PinRead(uint32_t pin) { assert(pin < PIN_COUNT); return pins[pin]; }
bool PORT_PinLatchRead(uint32_t pin) { return PORT_PinRead(pin); }
void LED_Clear(void) {}
bool SERCOM3_USART_ReceiverIsReady(void) { return input_position < input_size; }
USART_ERROR SERCOM3_USART_ErrorGet(void) { return USART_ERROR_NONE; }
int SERCOM3_USART_ReadByte(void) { assert(input_position < input_size); return input[input_position++]; }
bool SERCOM3_USART_Read(void *buffer, size_t size) { (void)buffer; (void)size; return false; }
bool SERCOM3_USART_TransmitterIsReady(void) {
    struct pollfd descriptor = {.fd = STDOUT_FILENO, .events = POLLOUT};
    return poll(&descriptor, 1U, 0) == 1 && (descriptor.revents & POLLOUT) != 0;
}
void SERCOM3_USART_WriteByte(int value) { uint8_t byte = (uint8_t)value; (void)write(STDOUT_FILENO, &byte, 1U); }
bool SERCOM3_USART_Write(void *buffer, size_t size) { (void)buffer; (void)size; return false; }
TickType_t xTaskGetTickCount(void) {
    uint64_t start_ms = (uint64_t)started.tv_sec * 1000U + (uint64_t)started.tv_nsec / 1000000U;
    return (TickType_t)(monotonic_ms() - start_ms);
}
void vTaskDelay(TickType_t ticks) {
    struct timespec delay = {.tv_sec = ticks / 1000U, .tv_nsec = (long)(ticks % 1000U) * 1000000L};
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {}
}

int main(int argc, char **argv) {
    (void)argc;
    (void)clock_gettime(CLOCK_MONOTONIC, &started);
    (void)fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    (void)fcntl(STDOUT_FILENO, F_SETFL, fcntl(STDOUT_FILENO, F_GETFL) | O_NONBLOCK);
    pins[FAULT1_PIN] = pins[FAULT2_PIN] = true;
    pins[CHRG_PIN] = true;
    const char *cause = getenv("PDU_SIM_RESET_CAUSE");
    test_rstc_regs.RSTC_RCAUSE = cause ? (uint8_t)strtoul(cause, NULL, 0) : 1U;
    APP_Initialize();
    uint64_t reset_at = 0U;
    for (;;) {
        if (input_position == input_size) {
            ssize_t received = read(STDIN_FILENO, input, sizeof(input));
            if (received == 0) return 0;
            if (received > 0) { input_size = (size_t)received; input_position = 0U; }
            else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) return 2;
        }
        APP_Tasks();
        if (pdu_software_reset_requested()) {
            /* The watchdog/electrical reset latency is modeled, not hardware-qualified. */
            if (!reset_at) reset_at = monotonic_ms() + 1000U;
            if (monotonic_ms() >= reset_at) {
                (void)setenv("PDU_SIM_RESET_CAUSE", "0x20", 1);
                execv(argv[0], argv);
                return 3;
            }
        }
        vTaskDelay(1U);
    }
}
