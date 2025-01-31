#ifndef RISCV_IRQ
#define RISCV_IRQ

#include <stdint.h>

/* The source id can refer to qemu
 * - https://github.com/qemu/qemu/blob/master/include/hw/riscv/virt.h#L65
 */

enum {
    UART0_IRQ = 10,
    VIRTIO_IRQ = 1,
};

typedef struct {
    enum {
        UserSoftwareInterrupt = 0,
        SupervisorSoftwareInterrupt = 1,
        MachineSoftwareInterrupt = 3,
        UserTimerInterrupt = 4,
        SupervisorTimerInterrupt = 5,
        MachineTimerInterrupt = 7,
        UserExternalInterrupt = 8,
        SupervisorExternalInterrupt = 9,
        MachineExternalInterrupt = 11,
        // extra number to represent no interrput for error checking
        NoInterrupt = 99,
    } irq;
    uint64_t value;
} riscv_irq;

#endif
