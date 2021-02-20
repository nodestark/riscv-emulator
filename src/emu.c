#include "emu.h"
#include "stdlib.h"

bool init_emu(riscv_emu *emu, const char *filename)
{
    if (!init_cpu(&emu->cpu, filename))
        return false;

    return true;
}

void start_emu(riscv_emu *emu)
{
    uint64_t start_pc = emu->cpu.pc;
    while (emu->cpu.pc < start_pc + emu->cpu.bus.memory.code_size &&
           emu->cpu.pc >= DRAM_BASE) {
        bool ret = true;

        ret = fetch(&emu->cpu);
        if (!ret)
            goto get_trap;

        emu->cpu.pc += 4;

        ret = decode(&emu->cpu);
        if (!ret)
            goto get_trap;

        ret = exec(&emu->cpu);

    get_trap:
        if (!ret) {
            Trap trap = take_trap(&emu->cpu);
            if (trap == Trap_Fatal) {
                LOG_ERROR("Trap %x happen when pc %lx", trap, emu->cpu.pc);
                break;
            }
        }

#ifdef DEBUG
        dump_reg(&emu->cpu);
        dump_csr(&emu->cpu);
#endif
    }

    dump_reg(&emu->cpu);
    dump_csr(&emu->cpu);
}

void close_emu(riscv_emu *emu)
{
    free_cpu(&emu->cpu);
    free(emu);
}
