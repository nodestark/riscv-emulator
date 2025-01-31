#ifndef RISCV_UART
#define RISCV_UART

/* Reference: http://byterunner.com/16550.html */

#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#include "exception.h"
#include "memmap.h"

// Receive holding register (read mode)
#define UART_RHR UART_BASE + 0
// Transmit holding register (write mode)
#define UART_THR UART_BASE + 0
// Interrupt enable register
#define UART_IER UART_BASE + 1
// Interrupt status register (read mode)
#define UART_ISR UART_BASE + 2
// FIFO control register (write mode)
#define UART_FCR UART_BASE + 2
// Line control register
#define UART_LCR UART_BASE + 3

// Line status register
#define UART_LSR UART_BASE + 5
/* BIT 0:
 * 0 = no data in receive holding register or FIFO.
 * 1 = data has been receive and saved in the receive holding register or FIFO.
 */
#define UART_LSR_RX 0x1
/* BIT 5:
 * 0 = transmit holding register is full. 16550 will not accept any data for
 * transmission. 1 = transmitter hold register (or FIFO) is empty. CPU can load
 * the next character.
 */
#define UART_LSR_TX 0x20
/* BIT 1:
 * 0 = disable the transmitter empty interrupt.
 * 1 = enable the transmitter empty interrupt.
 */
#define UART_IER_THR_EMPTY_INT 0x2

typedef struct {
    uint8_t reg[UART_SIZE];
    bool is_interrupt;
    pthread_t child_tid;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} riscv_uart;

bool init_uart(riscv_uart *uart);
uint64_t read_uart(riscv_uart *uart,
                   uint64_t addr,
                   uint8_t size,
                   riscv_exception *exc);
bool write_uart(riscv_uart *uart,
                uint64_t addr,
                uint8_t size,
                uint64_t value,
                riscv_exception *exc);
bool uart_is_interrupt(riscv_uart *uart);
void free_uart(riscv_uart *uart);

#endif
