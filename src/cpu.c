#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"

static uint64_t read_cpu(riscv_cpu *cpu, uint64_t addr, uint8_t size);
static bool write_cpu(riscv_cpu *cpu,
                      uint64_t addr,
                      uint8_t size,
                      uint64_t value);

/* Many type conversion are appied for expected result. To know the detail, you
 * should check out the International Standard of C:
 * http://www.open-std.org/jtc1/sc22/wg14/www/docs/n1548.pdf
 *
 * For example, belows are some C standard you may want to know:
 * 1. The result of E1 >> E2 is E1 right-shifted E2 bit positions.
 * If E1 has an unsigned type the value of the result is the integral
 * part of the quotient of E1 / 2E2.  If E1 has a signed type and a negative
 * value, the resulting value is implementation-defined.
 *
 * 2. A computation involving unsigned operands can never overflow,
 * because a result that cannot be represented by the resulting unsigned
 * integer type is reduced modulo the number that is one greater than the
 * largest value that can be represented by the resulting type
 *
 * 3. the new type is signed and the value cannot be represented in it;
 * either the result is implementation-defined or an implementation-defined
 * signal is raised.
 *
 * Implementation-defined of gcc:
 * https://gcc.gnu.org/onlinedocs/gcc/Integers-implementation.html
 *
 * The important rule of gcc implementation we will use here:
 * 1. The result of, or the signal raised by, converting an integer to a signed
 * integer type when the value cannot be represented in an object of that type
 * (C90 6.2.1.2, C99 and C11 6.3.1.3): For conversion to a type of width N, the
 * value is reduced modulo 2^N to be within range of the type; no signal is
 * raised.
 *
 * 2. The results of some bitwise operations on signed integers (C90 6.3, C99
 * and C11 6.5): Bitwise operators act on the representation of the value
 * including both the sign and value bits, where the sign bit is considered
 * immediately above the highest-value value bit. Signed ‘>>’ acts on negative
 * numbers by sign extension. As an extension to the C language, GCC does not
 * use the latitude given in C99 and C11 only to treat certain aspects of signed
 * ‘<<’ as undefined. However, -fsanitize=shift (and -fsanitize=undefined) will
 * diagnose such cases. They are also diagnosed where constant expressions are
 * required.
 *
 * On the other words, use compiler other than gcc may result error!
 */

static void instr_lb(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    uint64_t value = read_cpu(cpu, addr, 8);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = ((int8_t) (value));
}

static void instr_lh(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    uint64_t value = read_cpu(cpu, addr, 16);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = ((int16_t) (value));
}

static void instr_lw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    uint64_t value = read_cpu(cpu, addr, 32);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = ((int32_t) (value));
}

static void instr_ld(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    uint64_t value = read_cpu(cpu, addr, 64);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = value;
}

static void instr_lbu(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    uint64_t value = read_cpu(cpu, addr, 8);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = value;
}

static void instr_lhu(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    uint64_t value = read_cpu(cpu, addr, 16);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = value;
}

static void instr_lwu(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    uint64_t value = read_cpu(cpu, addr, 32);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = value;
}

static void instr_fence(__attribute__((unused)) riscv_cpu *cpu)
{
    /* Since our emulator only execute instruction on a single thread.
     * So nothing will do for fence instruction */
}

static void instr_fencei(__attribute__((unused)) riscv_cpu *cpu)
{
    /* A FENCE.I instruction ensures that a subsequent instruction fetch on a
     * RISC-V
     * hart will see any previous data stores already visible to the same RISC-V
     * hart. */
#ifdef ICACHE_CONFIG
    invalid_icache(&cpu->icache);
#endif
}

static void instr_addi(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
}

static void instr_slli(riscv_cpu *cpu)
{
    // shift amount is the lower 6 bits of immediate
    uint32_t shamt = (cpu->instr.imm & 0x3f);
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1] << shamt;
}

static void instr_mulh(riscv_cpu *cpu)
{
    /* FIXME: we are using the gcc extension, maybe we'll need another
     * portable version */
    int64_t rs1 = cpu->xreg[cpu->instr.rs1];
    int64_t rs2 = cpu->xreg[cpu->instr.rs2];

    __int128_t result = (__int128_t) rs1 * (__int128_t) rs2;
    cpu->xreg[cpu->instr.rd] = result >> 64;
}

static void instr_slti(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        ((int64_t) cpu->xreg[cpu->instr.rs1] < (int64_t) cpu->instr.imm) ? 1
                                                                         : 0;
}

static void instr_sltiu(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        (cpu->xreg[cpu->instr.rs1] < cpu->instr.imm) ? 1 : 0;
}

static void instr_xori(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1] ^ cpu->instr.imm;
}

static void instr_srli(riscv_cpu *cpu)
{
    // shift amount is the lower 6 bits of immediate
    uint32_t shamt = (cpu->instr.imm & 0x3f);
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1] >> shamt;
}

static void instr_srai(riscv_cpu *cpu)
{
    // shift amount is the lower 6 bits of immediate
    uint32_t shamt = (cpu->instr.imm & 0x3f);
    cpu->xreg[cpu->instr.rd] = (int64_t) (cpu->xreg[cpu->instr.rs1]) >> shamt;
}

static void instr_ori(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1] | cpu->instr.imm;
}

static void instr_andi(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1] & cpu->instr.imm;
}

static void instr_add(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rs1] + cpu->xreg[cpu->instr.rs2];
}

static void instr_mul(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rs1] * cpu->xreg[cpu->instr.rs2];
}

static void instr_sub(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rs1] - cpu->xreg[cpu->instr.rs2];
}

static void instr_sll(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1]
                               << cpu->xreg[cpu->instr.rs2];
}

static void instr_slt(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = ((int64_t) cpu->xreg[cpu->instr.rs1] <
                                (int64_t) cpu->xreg[cpu->instr.rs2])
                                   ? 1
                                   : 0;
}

static void instr_mulhsu(riscv_cpu *cpu)
{
    /* FIXME: we are using the gcc extension, maybe we'll need another
     * portable version */
    int64_t rs1 = cpu->xreg[cpu->instr.rs1];
    uint64_t rs2 = cpu->xreg[cpu->instr.rs2];

    __int128_t result = (__int128_t) rs1 * (__uint128_t) rs2;
    cpu->xreg[cpu->instr.rd] = result >> 64;
}

static void instr_sltu(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        (cpu->xreg[cpu->instr.rs1] < cpu->xreg[cpu->instr.rs2]) ? 1 : 0;
}

static void instr_mulhu(riscv_cpu *cpu)
{
    /* FIXME: we are using the gcc extension, maybe we'll need another
     * portable version */
    uint64_t rs1 = cpu->xreg[cpu->instr.rs1];
    uint64_t rs2 = cpu->xreg[cpu->instr.rs2];

    __uint128_t result = (__uint128_t) rs1 * (__uint128_t) rs2;
    cpu->xreg[cpu->instr.rd] = result >> 64;
}

static void instr_xor(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rs1] ^ cpu->xreg[cpu->instr.rs2];
}

static void instr_div(riscv_cpu *cpu)
{
    int64_t dividend = cpu->xreg[cpu->instr.rs1];
    int64_t divisor = cpu->xreg[cpu->instr.rs2];

    if (divisor == 0) {
        /* TODO: set DZ (Divide by Zero) in the FCSR */

        // the quotient of division by zero has all bits set
        cpu->xreg[cpu->instr.rd] = -1;
    } else if (dividend == INT64_MIN && divisor == -1) {
        /* 1. Signed division overflow occurs only when the most-negative
         * integer is divided by −1
         *
         * 2. The quotient of a signed division with overflow is equal to the
         * dividend*/
        cpu->xreg[cpu->instr.rd] = dividend;
    } else {
        cpu->xreg[cpu->instr.rd] = dividend / divisor;
    }
}

static void instr_srl(riscv_cpu *cpu)
{
    uint32_t shamt = (cpu->xreg[cpu->instr.rs2] & 0x3f);
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs1] >> shamt;
}

static void instr_divu(riscv_cpu *cpu)
{
    uint64_t dividend = cpu->xreg[cpu->instr.rs1];
    uint64_t divisor = cpu->xreg[cpu->instr.rs2];

    if (divisor == 0) {
        /* TODO: set DZ (Divide by Zero) in the FCSR */

        // the quotient of division by zero has all bits set
        cpu->xreg[cpu->instr.rd] = -1;
    } else {
        cpu->xreg[cpu->instr.rd] = dividend / divisor;
    }
}

static void instr_sra(riscv_cpu *cpu)
{
    // shift amount is the low 6 bits of rs2
    uint32_t shamt = (cpu->xreg[cpu->instr.rs2] & 0x3f);
    cpu->xreg[cpu->instr.rd] = (int64_t) cpu->xreg[cpu->instr.rs1] >> shamt;
}

static void instr_or(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rs1] | cpu->xreg[cpu->instr.rs2];
}

static void instr_rem(riscv_cpu *cpu)
{
    int64_t dividend = cpu->xreg[cpu->instr.rs1];
    int64_t divisor = cpu->xreg[cpu->instr.rs2];

    if (divisor == 0) {
        // the remainder of division by zero equals the dividend
        cpu->xreg[cpu->instr.rd] = dividend;
    } else if (dividend == INT64_MIN && divisor == -1) {
        /* The remainder with overflow is zero. */
        cpu->xreg[cpu->instr.rd] = 0;
    } else {
        cpu->xreg[cpu->instr.rd] = dividend % divisor;
    }
}

static void instr_and(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rs1] & cpu->xreg[cpu->instr.rs2];
}

static void instr_remu(riscv_cpu *cpu)
{
    uint64_t dividend = cpu->xreg[cpu->instr.rs1];
    uint64_t divisor = cpu->xreg[cpu->instr.rs2];

    if (divisor == 0) {
        /* TODO: set DZ (Divide by Zero) in the FCSR */

        // the quotient of division by zero has all bits set
        cpu->xreg[cpu->instr.rd] = dividend;
    } else {
        cpu->xreg[cpu->instr.rd] = dividend % divisor;
    }
}

static void instr_auipc(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->pc + cpu->instr.imm - 4;
}

static void instr_addiw(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = (int32_t) ((
        (uint32_t) cpu->xreg[cpu->instr.rs1] + (uint32_t) cpu->instr.imm));
}

static void instr_slliw(riscv_cpu *cpu)
{
    uint32_t shamt = (cpu->instr.imm & 0x1f);
    cpu->xreg[cpu->instr.rd] =
        (int32_t) (((uint32_t) cpu->xreg[cpu->instr.rs1] << shamt));
}

static void instr_srliw(riscv_cpu *cpu)
{
    uint32_t shamt = (cpu->instr.imm & 0x1f);
    cpu->xreg[cpu->instr.rd] =
        (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rs1] >> shamt);
}

static void instr_sraiw(riscv_cpu *cpu)
{
    uint32_t shamt = (cpu->instr.imm & 0x1f);
    cpu->xreg[cpu->instr.rd] =
        (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rs1]) >> shamt;
}

static void instr_sb(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    write_cpu(cpu, addr, 8, cpu->xreg[cpu->instr.rs2]);
}

static void instr_sh(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    write_cpu(cpu, addr, 16, cpu->xreg[cpu->instr.rs2]);
}

static void instr_sw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    write_cpu(cpu, addr, 32, cpu->xreg[cpu->instr.rs2]);
}

static void instr_sd(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    write_cpu(cpu, addr, 64, cpu->xreg[cpu->instr.rs2]);
}

static void instr_fsw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    float32_reg_t f32;
    f32.f = cpu->freg[cpu->instr.rs2].f;
    write_cpu(cpu, addr, 32, f32.u);
}

static void instr_fsd(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1] + cpu->instr.imm;
    write_cpu(cpu, addr, 64, cpu->freg[cpu->instr.rs2].u);
}

static void instr_lui(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->instr.imm;
}

static void instr_addw(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rs1] +
                                          (uint32_t) cpu->xreg[cpu->instr.rs2]);
}

static void instr_mulw(riscv_cpu *cpu)
{
    int32_t rs1 = cpu->xreg[cpu->instr.rs1] & 0xffffffff;
    int32_t rs2 = cpu->xreg[cpu->instr.rs2] & 0xffffffff;
    cpu->xreg[cpu->instr.rd] = rs1 * rs2;
}

static void instr_subw(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rs1] -
                                          (uint32_t) cpu->xreg[cpu->instr.rs2]);
}

static void instr_sllw(riscv_cpu *cpu)
{
    uint32_t shamt = (cpu->xreg[cpu->instr.rs2] & 0x1f);
    cpu->xreg[cpu->instr.rd] =
        (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rs1] << shamt);
}

static void instr_divw(riscv_cpu *cpu)
{
    int32_t dividend = cpu->xreg[cpu->instr.rs1] & 0xffffffff;
    int32_t divisor = cpu->xreg[cpu->instr.rs2] & 0xffffffff;

    if (divisor == 0) {
        /* TODO: set DZ (Divide by Zero) in the FCSR */

        // the quotient of division by zero has all bits set
        cpu->xreg[cpu->instr.rd] = -1;
    } else if (dividend == INT32_MIN && divisor == -1) {
        /* 1. Signed division overflow occurs only when the most-negative
         * integer is divided by −1
         *
         * 2. The quotient of a signed division with overflow is equal to the
         * dividend*/
        cpu->xreg[cpu->instr.rd] = dividend;
    } else {
        cpu->xreg[cpu->instr.rd] = dividend / divisor;
    }
}

static void instr_srlw(riscv_cpu *cpu)
{
    uint32_t shamt = (cpu->xreg[cpu->instr.rs2] & 0x1f);
    cpu->xreg[cpu->instr.rd] =
        (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rs1] >> shamt);
}

static void instr_divuw(riscv_cpu *cpu)
{
    uint32_t dividend = cpu->xreg[cpu->instr.rs1];
    uint32_t divisor = cpu->xreg[cpu->instr.rs2];

    if (divisor == 0) {
        /* TODO: set DZ (Divide by Zero) in the FCSR */

        // the quotient of division by zero has all bits set
        cpu->xreg[cpu->instr.rd] = -1;
    } else {
        cpu->xreg[cpu->instr.rd] = (int32_t) (dividend / divisor);
    }
}

static void instr_sraw(riscv_cpu *cpu)
{
    uint32_t shamt = (cpu->xreg[cpu->instr.rs2] & 0x1f);
    cpu->xreg[cpu->instr.rd] =
        (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rs1]) >> shamt;
}

static void instr_remw(riscv_cpu *cpu)
{
    int32_t dividend = cpu->xreg[cpu->instr.rs1] & 0xffffffff;
    int32_t divisor = cpu->xreg[cpu->instr.rs2] & 0xffffffff;

    if (divisor == 0) {
        // the remainder of division by zero equals the dividend
        cpu->xreg[cpu->instr.rd] = dividend;
    } else if (dividend == INT32_MIN && divisor == -1) {
        /* The remainder with overflow is zero. */
        cpu->xreg[cpu->instr.rd] = 0;
    } else {
        cpu->xreg[cpu->instr.rd] = dividend % divisor;
    }
}

static void instr_remuw(riscv_cpu *cpu)
{
    uint32_t dividend = cpu->xreg[cpu->instr.rs1];
    uint32_t divisor = cpu->xreg[cpu->instr.rs2];

    // REMUW always sign-extend the 32-bit result to 64 bits, including on a
    // divide by zero.
    if (divisor == 0) {
        // the remainder of division by zero equals the dividend
        cpu->xreg[cpu->instr.rd] = (int32_t) dividend;
    } else {
        cpu->xreg[cpu->instr.rd] = (int32_t) (dividend % divisor);
    }
}

static void instr_beq(riscv_cpu *cpu)
{
    if (cpu->xreg[cpu->instr.rs1] == cpu->xreg[cpu->instr.rs2])
        cpu->pc = cpu->pc + cpu->instr.imm - 4;
}

static void instr_bne(riscv_cpu *cpu)
{
    if (cpu->xreg[cpu->instr.rs1] != cpu->xreg[cpu->instr.rs2])
        cpu->pc = cpu->pc + cpu->instr.imm - 4;
}

static void instr_blt(riscv_cpu *cpu)
{
    if ((int64_t) cpu->xreg[cpu->instr.rs1] <
        (int64_t) cpu->xreg[cpu->instr.rs2])
        cpu->pc = cpu->pc + cpu->instr.imm - 4;
}

static void instr_bge(riscv_cpu *cpu)
{
    if ((int64_t) cpu->xreg[cpu->instr.rs1] >=
        (int64_t) cpu->xreg[cpu->instr.rs2])
        cpu->pc = cpu->pc + cpu->instr.imm - 4;
}

static void instr_bltu(riscv_cpu *cpu)
{
    if (cpu->xreg[cpu->instr.rs1] < cpu->xreg[cpu->instr.rs2])
        cpu->pc = cpu->pc + cpu->instr.imm - 4;
}

static void instr_bgeu(riscv_cpu *cpu)
{
    if (cpu->xreg[cpu->instr.rs1] >= cpu->xreg[cpu->instr.rs2])
        cpu->pc = cpu->pc + cpu->instr.imm - 4;
}

static void instr_jalr(riscv_cpu *cpu)
{
    // we don't need to add 4 because the pc already moved on.
    uint64_t prev_pc = cpu->pc;

    // note that we have to set the least-significant bit of the result to zero
    cpu->pc = (cpu->xreg[cpu->instr.rs1] + cpu->instr.imm) & ~1;
    cpu->xreg[cpu->instr.rd] = prev_pc;
}

static void instr_jal(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = cpu->pc;
    cpu->pc = cpu->pc + cpu->instr.imm - 4;
}

static void instr_ecall(riscv_cpu *cpu)
{
    assert(cpu->instr.instr & 0x73);

    cpu->exc.value = cpu->pc - 4;
    switch (cpu->mode.mode) {
    case MACHINE:
        cpu->exc.exception = EnvironmentCallFromMMode;
        return;
    case SUPERVISOR:
        cpu->exc.exception = EnvironmentCallFromSMode;
        return;
    case USER:
        cpu->exc.exception = EnvironmentCallFromUMode;
        return;
    default:
        cpu->exc.exception = IllegalInstruction;
        cpu->exc.value = 0;
        return;
    }
}

static void instr_ebreak(riscv_cpu *cpu)
{
    cpu->exc.exception = Breakpoint;
}

static void instr_sret(riscv_cpu *cpu)
{
    cpu->pc = read_csr(&cpu->csr, SEPC);
    /* When an SRET instruction is executed to return from the trap handler, the
     * privilege level is set to user mode if the SPP bit is 0, or supervisor
     * mode if the SPP bit is 1; SPP is then set to 0 */
    uint64_t sstatus = read_csr(&cpu->csr, SSTATUS);
    cpu->mode.mode = (sstatus & SSTATUS_SPP) >> 8;

    /* When an SRET instruction is executed, SIE is set to SPIE */
    /* Since SPIE is at bit 5 and SIE is at bit 1, so we right shift 4 bit for
     * correct value */
    write_csr(&cpu->csr, SSTATUS,
              (sstatus & ~SSTATUS_SIE) | ((sstatus & SSTATUS_SPIE) >> 4));
    /* SPIE is set to 1 */
    set_csr_bits(&cpu->csr, SSTATUS, SSTATUS_SPIE);
    /* SPP is set to 0 */
    clear_csr_bits(&cpu->csr, SSTATUS, SSTATUS_SPP);

#ifdef ICACHE_CONFIG
    // flush cache when returning from supervisor mode
    invalid_icache(&cpu->icache);
#endif
}

static void instr_mret(riscv_cpu *cpu)
{
    cpu->pc = read_csr(&cpu->csr, MEPC);
    uint64_t mstatus = read_csr(&cpu->csr, MSTATUS);
    cpu->mode.mode = (mstatus & MSTATUS_MPP) >> 11;

    write_csr(&cpu->csr, MSTATUS,
              (mstatus & ~MSTATUS_MIE) | ((mstatus & MSTATUS_MPIE) >> 4));
    set_csr_bits(&cpu->csr, MSTATUS, MSTATUS_MPIE);
    clear_csr_bits(&cpu->csr, MSTATUS, MSTATUS_MPP);

#ifdef ICACHE_CONFIG
    // flush cache when returning from machine mode
    invalid_icache(&cpu->icache);
#endif
}

static void instr_wfi(__attribute__((unused)) riscv_cpu *cpu) {}

static void instr_sfencevma(__attribute__((unused)) riscv_cpu *cpu)
{
#ifdef ICACHE_CONFIG
    /* FIXME: What is ASID? How should we support this? */

    /* If rs1 = x0, the fence orders all reads and writes made to any level
     * of the page tables */
    if (cpu->instr.rs1 == 0) {
        invalid_icache(&cpu->icache);
    }
    /* If rs1 != x0, the fence orders only reads and writes made to
     * the leaf page table entry corresponding to the virtual address in rs1 */
    else {
        invalid_icache_by_vaddr(&cpu->icache, cpu->xreg[cpu->instr.rs1]);
    }
#endif
}

static void instr_hfencebvma(__attribute__((unused)) riscv_cpu *cpu) {}

static void instr_hfencegvma(__attribute__((unused)) riscv_cpu *cpu) {}

static void instr_csrrw(riscv_cpu *cpu)
{
    uint64_t tmp = read_csr(&cpu->csr, cpu->instr.imm);
    write_csr(&cpu->csr, cpu->instr.imm, cpu->xreg[cpu->instr.rs1]);
    cpu->xreg[cpu->instr.rd] = tmp;
}

static void instr_csrrs(riscv_cpu *cpu)
{
    uint64_t tmp = read_csr(&cpu->csr, cpu->instr.imm);
    write_csr(&cpu->csr, cpu->instr.imm, tmp | cpu->xreg[cpu->instr.rs1]);
    cpu->xreg[cpu->instr.rd] = tmp;
}

static void instr_csrrc(riscv_cpu *cpu)
{
    uint64_t tmp = read_csr(&cpu->csr, cpu->instr.imm);
    write_csr(&cpu->csr, cpu->instr.imm, tmp & (~cpu->xreg[cpu->instr.rs1]));
    cpu->xreg[cpu->instr.rd] = tmp;
}

static void instr_csrrwi(riscv_cpu *cpu)
{
    uint64_t zimm = cpu->instr.rs1;
    cpu->xreg[cpu->instr.rd] = read_csr(&cpu->csr, cpu->instr.imm);
    write_csr(&cpu->csr, cpu->instr.imm, zimm);
}

static void instr_csrrsi(riscv_cpu *cpu)
{
    uint64_t zimm = cpu->instr.rs1;
    uint64_t tmp = read_csr(&cpu->csr, cpu->instr.imm);
    write_csr(&cpu->csr, cpu->instr.imm, tmp | zimm);
    cpu->xreg[cpu->instr.rd] = tmp;
}

static void instr_csrrci(riscv_cpu *cpu)
{
    uint64_t zimm = cpu->instr.rs1;
    uint64_t tmp = read_csr(&cpu->csr, cpu->instr.imm);
    write_csr(&cpu->csr, cpu->instr.imm, tmp & (~zimm));
    cpu->xreg[cpu->instr.rd] = tmp;
}

/* TODO: the lock acquire and realease are not implemented now */
static void instr_amoaddw(riscv_cpu *cpu)
{
    uint64_t tmp = read_cpu(cpu, cpu->xreg[cpu->instr.rs1], 32);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }
    if (!write_cpu(cpu, cpu->xreg[cpu->instr.rs1], 32,
                   tmp + cpu->xreg[cpu->instr.rs2]))
        return;
    // For RV64, 32-bit AMOs always sign-extend the value placed in rd.
    cpu->xreg[cpu->instr.rd] = (int32_t) (tmp & 0xffffffff);
}

static void instr_amoswapw(riscv_cpu *cpu)
{
    uint64_t tmp = read_cpu(cpu, cpu->xreg[cpu->instr.rs1], 32);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }
    if (!write_cpu(cpu, cpu->xreg[cpu->instr.rs1], 32,
                   cpu->xreg[cpu->instr.rs2]))
        return;
    cpu->xreg[cpu->instr.rd] = (int32_t) (tmp & 0xffffffff);
}

static void instr_lrw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 32);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = (int32_t) (tmp & 0xffffffff);
    cpu->reservation = addr;
}

static void instr_scw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];

    if (cpu->reservation == addr) {
        if (!write_cpu(cpu, addr, 32, cpu->xreg[cpu->instr.rs2]))
            return;
        cpu->xreg[cpu->instr.rd] = 0;
    } else {
        cpu->xreg[cpu->instr.rd] = 1;
    }

    // invalidate the reservation
    cpu->reservation = (uint64_t) -1;
}

static void instr_amoxorw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 32);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }

    uint64_t value = (int32_t) ((tmp ^ cpu->xreg[cpu->instr.rs2]) & 0xffffffff);
    if (!write_cpu(cpu, addr, 32, value))
        return;

    cpu->xreg[cpu->instr.rd] = (int32_t) (tmp & 0xffffffff);
}

static void instr_amoorw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 32);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }

    uint64_t value = (int32_t) ((tmp | cpu->xreg[cpu->instr.rs2]) & 0xffffffff);
    if (!write_cpu(cpu, addr, 32, value))
        return;

    cpu->xreg[cpu->instr.rd] = (int32_t) (tmp & 0xffffffff);
}

static void instr_amoandw(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 32);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }

    uint64_t value = (int32_t) ((tmp & cpu->xreg[cpu->instr.rs2]) & 0xffffffff);
    if (!write_cpu(cpu, addr, 32, value))
        return;

    cpu->xreg[cpu->instr.rd] = (int32_t) (tmp & 0xffffffff);
}

/*
static void instr_amominw(riscv_cpu *cpu){}
static void instr_amomaxw(riscv_cpu *cpu){}
*/

static void instr_amoaddd(riscv_cpu *cpu)
{
    uint64_t tmp = read_cpu(cpu, cpu->xreg[cpu->instr.rs1], 64);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }
    if (!write_cpu(cpu, cpu->xreg[cpu->instr.rs1], 64,
                   tmp + cpu->xreg[cpu->instr.rs2]))
        return;
    cpu->xreg[cpu->instr.rd] = tmp;
}

static void instr_amoswapd(riscv_cpu *cpu)
{
    uint64_t tmp = read_cpu(cpu, cpu->xreg[cpu->instr.rs1], 64);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }
    if (!write_cpu(cpu, cpu->xreg[cpu->instr.rs1], 64,
                   cpu->xreg[cpu->instr.rs2]))
        return;
    cpu->xreg[cpu->instr.rd] = tmp;
}


static void instr_lrd(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 64);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = tmp;
    cpu->reservation = addr;
}


static void instr_scd(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];

    if (cpu->reservation == addr) {
        if (!write_cpu(cpu, addr, 64, cpu->xreg[cpu->instr.rs2]))
            return;
        cpu->xreg[cpu->instr.rd] = 0;
    } else {
        cpu->xreg[cpu->instr.rd] = 1;
    }

    // invalidate the reservation
    cpu->reservation = (uint64_t) -1;
}


static void instr_amoxord(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 64);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }

    uint64_t value = tmp ^ cpu->xreg[cpu->instr.rs2];
    if (!write_cpu(cpu, addr, 64, value))
        return;

    cpu->xreg[cpu->instr.rd] = tmp;
}

static void instr_amoord(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 64);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }

    uint64_t value = tmp | cpu->xreg[cpu->instr.rs2];
    if (!write_cpu(cpu, addr, 64, value))
        return;

    cpu->xreg[cpu->instr.rd] = tmp;
}

static void instr_amoandd(riscv_cpu *cpu)
{
    uint64_t addr = cpu->xreg[cpu->instr.rs1];
    uint64_t tmp = read_cpu(cpu, addr, 64);
    if (cpu->exc.exception != NoException) {
        assert(tmp == (uint64_t) -1);
        return;
    }

    uint64_t value = tmp & cpu->xreg[cpu->instr.rs2];
    if (!write_cpu(cpu, addr, 64, value))
        return;

    cpu->xreg[cpu->instr.rd] = tmp;
}

/*
static void instr_amomind(riscv_cpu *cpu){}
static void instr_amomaxd(riscv_cpu *cpu){}
*/

static void instr_caddi4spn(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // nzuimm[5:4|9:6|2|3] = inst[12:11|10:7|6|5]
    uint64_t nzuimm = ((instr >> 1) & 0x3c0) | ((instr >> 7) & 0x30) |
                      ((instr >> 2) & 0x8) | ((instr >> 4) & 0x4);
    // C.ADDI4SPN is only valid when nzuimm !=0
    if (nzuimm != 0) {
        cpu->xreg[cpu->instr.rd] = cpu->xreg[2] + nzuimm;
    }
}

static void instr_clw(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5:3|2|6] = inst[12:10|6|5]
    uint8_t offset =
        ((instr >> 7) & 0x38) | ((instr << 1) & 0x40) | ((instr >> 4) & 0x4);

    uint64_t addr = cpu->xreg[cpu->instr.rs1] + offset;
    uint64_t value = read_cpu(cpu, addr, 32);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = ((int32_t) value);
}

static void instr_cld(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5:3|7:6] = inst[12:10|6:5]
    uint8_t offset = ((instr >> 7) & 0x38) | ((instr << 1) & 0xc0);

    uint64_t addr = cpu->xreg[cpu->instr.rs1] + offset;
    uint64_t value = read_cpu(cpu, addr, 64);
    if (cpu->exc.exception != NoException) {
        assert(value == (uint64_t) -1);
        return;
    }
    cpu->xreg[cpu->instr.rd] = value;
}

static void instr_cfsd(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5:3|7:6] = inst[12:10|6:5]
    uint8_t offset = ((instr >> 7) & 0x38) | ((instr << 1) & 0xc0);

    uint64_t addr = cpu->xreg[cpu->instr.rs1] + offset;
    write_cpu(cpu, addr, 64, cpu->freg[cpu->instr.rs2].u);
}

static void instr_csw(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5:3|2|6] = inst[12:10|6|5]
    uint8_t offset =
        ((instr >> 7) & 0x38) | ((instr << 1) & 0x40) | ((instr >> 4) & 0x4);

    uint64_t addr = cpu->xreg[cpu->instr.rs1] + offset;
    write_cpu(cpu, addr, 32, cpu->xreg[cpu->instr.rs2]);
}

static void instr_csd(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5:3|7:6] = inst[12:10|6:5]
    uint8_t offset = ((instr >> 7) & 0x38) | ((instr << 1) & 0xc0);

    uint64_t addr = cpu->xreg[cpu->instr.rs1] + offset;
    write_cpu(cpu, addr, 64, cpu->xreg[cpu->instr.rs2]);
}

static void instr_caddi(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // imm[5|4:0] = inst[12|6:2], sign-extended 6-bit immediate
    uint64_t nzimm = ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1f);

    nzimm |= ((nzimm & 0x20) ? 0xFFFFFFFFFFFFFFC0 : 0);
    // C.ADDI is only valid when rd != x0 and nzimm != 0
    if (cpu->instr.rd != 0 && nzimm != 0) {
        cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rd] + nzimm;
    }
}

static void instr_caddiw(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // imm[5|4:0] = inst[12|6:2]
    uint32_t imm = ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1f);

    imm |= ((imm & 0x20) ? 0xFFFFFFC0 : 0);

    // C.ADDIW is only valid when rd != x0
    if (cpu->instr.rd != 0) {
        cpu->xreg[cpu->instr.rd] =
            (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rd] + imm);
    }
}

static void instr_cli(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // imm[5|4:0] = inst[12|6:2]
    uint64_t imm = ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1f);

    imm |= ((imm & 0x20) ? 0xFFFFFFFFFFFFFFC0 : 0);

    if (cpu->instr.rd) {
        cpu->xreg[cpu->instr.rd] = imm;
    }
}

static void instr_clui_caddi16sp(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    uint64_t rd = cpu->instr.rd;

    /* C.LUI loads the non-zero 6-bit immediate field into bits 17–12 of the
     * destination register, clears the bottom 12 bits, and sign-extends bit 17
     * into all higher bits of the destination.
     *
     * C.LUI is only valid when rd != {x0, x2}, and when the immediate is not
     * equal to zero. */
    if (rd != 0 && rd != 2) {
        // nzimm[17|16:12] = inst[12|6:2]
        uint64_t nzimm = ((instr << 5) & 0x20000) | ((instr << 10) & 0x1f000);
        nzimm |= ((nzimm & 0x20000) ? 0xFFFFFFFFFFFC0000 : 0);
        // The code points with nzimm=0 are reserved
        if (nzimm != 0)
            cpu->xreg[rd] = nzimm;
    }
    // rd==x2 correspond to the C.ADDI16SP instruction.
    else if (rd == 2) {
        // nzimm[9|4|6|8:7|5] = inst[12|6|5|4:3|2]
        uint64_t nzimm = ((instr >> 3) & 0x200) | ((instr >> 2) & 0x10) |
                         ((instr << 1) & 0x40) | ((instr << 4) & 0x180) |
                         ((instr << 3) & 0x20);

        nzimm |= ((nzimm & 0x200) ? 0xFFFFFFFFFFFFFC00 : 0);
        if (nzimm != 0)
            cpu->xreg[2] = cpu->xreg[2] + nzimm;
    }
}

static void instr_csrli(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // shamt[5|4:0] = inst[12|6:2]
    uint8_t shamt = ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1f);
    // the shift amount must be non-zero for RV64C
    if (shamt != 0) {
        cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rd] >> shamt;
    }
}

static void instr_csrai(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // shamt[5|4:0] = inst[12|6:2]
    uint8_t shamt = ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1f);
    // the shift amount must be non-zero for RV64C
    if (shamt != 0) {
        cpu->xreg[cpu->instr.rd] = (int64_t) cpu->xreg[cpu->instr.rd] >> shamt;
    }
}

static void instr_candi(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // shamt[5|4:0] = inst[12|6:2]
    uint64_t imm = ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1f);
    imm |= ((imm & 0x20) ? 0xFFFFFFFFFFFFFFC0 : 0);
    cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rd] & imm;
}

static void instr_csub(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rd] - cpu->xreg[cpu->instr.rs2];
}

static void instr_cxor(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rd] ^ cpu->xreg[cpu->instr.rs2];
}

static void instr_cor(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rd] | cpu->xreg[cpu->instr.rs2];
}

static void instr_cand(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] =
        cpu->xreg[cpu->instr.rd] & cpu->xreg[cpu->instr.rs2];
}

static void instr_csubw(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rd] -
                                          (uint32_t) cpu->xreg[cpu->instr.rs2]);
}

static void instr_caddw(riscv_cpu *cpu)
{
    cpu->xreg[cpu->instr.rd] = (int32_t) ((uint32_t) cpu->xreg[cpu->instr.rd] +
                                          (uint32_t) cpu->xreg[cpu->instr.rs2]);
}

static void instr_cj(riscv_cpu *cpu)
{
    cpu->instr.imm |= ((cpu->instr.imm & 0x800) ? 0xFFFFFFFFFFFFF000 : 0);
    cpu->pc = cpu->pc + cpu->instr.imm - 2;
}

static void instr_cbeqz(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // imm[8|4:3|7:6|2:1|5] = inst[12|11:10|6:5|4:3|2]
    uint64_t imm = ((instr >> 4) & 0x100) | ((instr << 1) & 0xc0) |
                   ((instr << 3) & 0x20) | ((instr >> 7) & 0x18) |
                   ((instr >> 2) & 0x6);

    imm |= (imm & 0x100) ? 0xFFFFFFFFFFFFFE00 : 0;

    if (cpu->xreg[cpu->instr.rs1] == 0)
        cpu->pc = cpu->pc + imm - 2;
}

static void instr_cbnez(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // imm[8|4:3|7:6|2:1|5] = inst[12|11:10|6:5|4:3|2]
    uint64_t imm = ((instr >> 4) & 0x100) | ((instr << 1) & 0xc0) |
                   ((instr << 3) & 0x20) | ((instr >> 7) & 0x18) |
                   ((instr >> 2) & 0x6);

    imm |= (imm & 0x100) ? 0xFFFFFFFFFFFFFE00 : 0;

    if (cpu->xreg[cpu->instr.rs1] != 0)
        cpu->pc = cpu->pc + imm - 2;
}

static void instr_cslli(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // shamt[5|4:0] = inst[12|6:2]
    uint8_t shamt = ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1f);

    if (shamt != 0) {
        cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rd] << shamt;
    }
}

static void instr_clwsp(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5|4:2|7:6] = inst[12|6:4|3:2]
    uint16_t offset =
        ((instr << 4) & 0xc0) | ((instr >> 7) & 0x20) | ((instr >> 2) & 0x1c);
    uint32_t val = read_cpu(cpu, cpu->xreg[2] + offset, 32);
    if (cpu->exc.exception != NoException) {
        assert(val == (uint32_t) -1);
        return;
    }
    // C.LWSP is only valid when rd != x0;
    if (cpu->instr.rd != 0) {
        cpu->xreg[cpu->instr.rd] = (int32_t) val;
    }
}

static void instr_cldsp(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5|4:3|8:6] = inst[12|6:5|4:2]
    uint16_t offset =
        ((instr << 4) & 0x1c0) | ((instr >> 7) & 0x20) | ((instr >> 2) & 0x18);
    uint64_t val = read_cpu(cpu, cpu->xreg[2] + offset, 64);
    if (cpu->exc.exception != NoException) {
        assert(val == (uint64_t) -1);
        return;
    }
    // C.LDSP is only valid when rd != x0;
    if (cpu->instr.rd != 0) {
        cpu->xreg[cpu->instr.rd] = val;
    }
}

static void instr_cjr_cmv(riscv_cpu *cpu)
{
    // C.JR
    if (cpu->instr.rs2 == 0) {
        // C.JR is only valid when rs1 != x0
        if (cpu->instr.rs1 != 0) {
            cpu->pc = cpu->xreg[cpu->instr.rs1];
        } else {
            cpu->exc.exception = IllegalInstruction;
            return;
        }
    }
    // C.MV
    else {
        cpu->xreg[cpu->instr.rd] = cpu->xreg[cpu->instr.rs2];
    }
}

static void instr_cebreak_cjalr_cadd(riscv_cpu *cpu)
{
    if (cpu->instr.rs2 == 0) {
        // C.EBREAK
        if (cpu->instr.rs1 == 0) {
            cpu->exc.exception = Breakpoint;
        }
        // C.JALR
        else {
            // C.JALR is only valid when rs1 != x0
            // we don't need to add 2 because the pc already moved on.
            uint64_t prev_pc = cpu->pc;
            cpu->pc = cpu->xreg[cpu->instr.rs1];
            cpu->xreg[1] = prev_pc;
        }
    }
    // C.ADD
    else {
        cpu->xreg[cpu->instr.rd] =
            cpu->xreg[cpu->instr.rd] + cpu->xreg[cpu->instr.rs2];
    }
}

static void instr_cswsp(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5:2|7:6] = inst[12:9|8:7]
    uint16_t offset = ((instr >> 1) & 0xc0) | ((instr >> 7) & 0x3c);

    uint64_t addr = cpu->xreg[2] + offset;
    write_cpu(cpu, addr, 32, cpu->xreg[cpu->instr.rs2]);
}

static void instr_csdsp(riscv_cpu *cpu)
{
    uint32_t instr = cpu->instr.instr;
    // offset[5:3|8:6] = inst[12:10|9:7]
    uint16_t offset = ((instr >> 1) & 0x1c0) | ((instr >> 7) & 0x38);

    uint64_t addr = cpu->xreg[2] + offset;
    write_cpu(cpu, addr, 64, cpu->xreg[cpu->instr.rs2]);
}

/* clang-format off */
#define INIT_RISCV_INSTR_LIST(_type, _instr)  \
    static riscv_instr_desc _instr##_list = { \
        {_type}, sizeof(_instr) / sizeof(_instr[0]), _instr}

static riscv_instr_entry instr_load_type[] = {
    [0x0] = {NULL, instr_lb, NULL, "LB"},
    [0x1] = {NULL, instr_lh, NULL, "LH"},
    [0x2] = {NULL, instr_lw, NULL, "LW"},
    [0x3] = {NULL, instr_ld, NULL, "LD"},
    [0x4] = {NULL, instr_lbu, NULL, "LBU"},
    [0x5] = {NULL, instr_lhu, NULL, "LHU"},
    [0x6] = {NULL, instr_lwu, NULL, "LWU"}
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_load_type);

static riscv_instr_entry instr_fence_type[] = {
    [0x0] = {NULL, instr_fence, NULL, "FENCE"},
    [0x1] = {NULL, instr_fencei, NULL, "FENCEI"}
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_fence_type);


static riscv_instr_entry instr_srli_srai_type[] = {
    [0x0] =  {NULL, instr_srli, NULL, "SRLI"},
    [0x10] = {NULL, instr_srai, NULL, "SRAI"}
};
INIT_RISCV_INSTR_LIST(FUNC7_S, instr_srli_srai_type);

static riscv_instr_entry instr_imm_type[] = {
    [0x0] = {NULL, instr_addi, NULL, "ADDI"},
    [0x1] = {NULL, instr_slli, NULL, "SLLI"},
    [0x2] = {NULL, instr_slti, NULL, "SLTI"},
    [0x3] = {NULL, instr_sltiu, NULL, "SLTIU"},
    [0x4] = {NULL, instr_xori, NULL, "XORI"},
    [0x5] = {NULL, NULL, &instr_srli_srai_type_list, NULL},
    [0x6] = {NULL, instr_ori, NULL, "ORI"},
    [0x7] = {NULL, instr_andi, NULL, "ANDI"}
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_imm_type);

static riscv_instr_entry instr_add_mul_sub_type[] = {
    [0x00] = {NULL, instr_add, NULL, "ADD"},
    [0x01] = {NULL, instr_mul, NULL, "MUL"},
    [0x20] = {NULL, instr_sub, NULL, "SUB"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_add_mul_sub_type);

static riscv_instr_entry instr_sll_mulh_type[] = {
    [0x00] = {NULL, instr_sll, NULL, "SLL"},
    [0x01] = {NULL, instr_mulh, NULL, "MULH"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_sll_mulh_type);

static riscv_instr_entry instr_slt_mulhsu_type[] = {
    [0x00] = {NULL, instr_slt, NULL, "SLT"},
    [0x01] = {NULL, instr_mulhsu, NULL, "MULHSU"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_slt_mulhsu_type);

static riscv_instr_entry instr_sltu_mulhu_type[] = {
    [0x00] = {NULL, instr_sltu, NULL, "SLTU"},
    [0x01] = {NULL, instr_mulhu, NULL, "MULHU"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_sltu_mulhu_type);

static riscv_instr_entry instr_xor_div_type[] = {
    [0x00] = {NULL, instr_xor, NULL, "XOR"},
    [0x01] = {NULL, instr_div, NULL, "DIV"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_xor_div_type);

static riscv_instr_entry instr_srl_divu_sra_type[] = {
    [0x00] = {NULL, instr_srl, NULL, "SRL"},
    [0x01] = {NULL, instr_divu, NULL, "DIVU"},
    [0x20] = {NULL, instr_sra, NULL, "SRA"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_srl_divu_sra_type);

static riscv_instr_entry instr_or_rem_type[] = {
    [0x00] = {NULL, instr_or, NULL, "OR"},
    [0x01] = {NULL, instr_rem, NULL, "REM"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_or_rem_type);

static riscv_instr_entry instr_and_remu_type[] = {
    [0x00] = {NULL, instr_and, NULL, "AND"},
    [0x01] = {NULL, instr_remu, NULL, "REMU"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_and_remu_type);

static riscv_instr_entry instr_reg_type[] = {
    [0x0] = {NULL, NULL, &instr_add_mul_sub_type_list, NULL},
    [0x1] = {NULL, NULL, &instr_sll_mulh_type_list, NULL},
    [0x2] = {NULL, NULL, &instr_slt_mulhsu_type_list, NULL},
    [0x3] = {NULL, NULL, &instr_sltu_mulhu_type_list, NULL},
    [0x4] = {NULL, NULL, &instr_xor_div_type_list, NULL},
    [0x5] = {NULL, NULL, &instr_srl_divu_sra_type_list, NULL},
    [0x6] = {NULL, NULL, &instr_or_rem_type_list, NULL},
    [0x7] = {NULL, NULL, &instr_and_remu_type_list, NULL}
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_reg_type);

static riscv_instr_entry instr_srliw_sraiw_type[] = {
    [0x00] = {NULL, instr_srliw, NULL, "SRLIW"},
    [0x20] = {NULL, instr_sraiw, NULL, "SRAIW"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_srliw_sraiw_type);

static riscv_instr_entry instr_immw_type[] = {
    [0x0] = {NULL, instr_addiw, NULL, "ADDIW"},
    [0x1] = {NULL, instr_slliw, NULL, "SLLIW"},
    [0x5] = {NULL, NULL, &instr_srliw_sraiw_type_list, NULL}
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_immw_type);

static riscv_instr_entry instr_store_type[] = {
    [0x0] = {NULL, instr_sb, NULL, "SB"},
    [0x1] = {NULL, instr_sh, NULL, "SH"},
    [0x2] = {NULL, instr_sw, NULL, "SW"},
    [0x3] = {NULL, instr_sd, NULL, "SD"},
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_store_type);

static riscv_instr_entry instr_store_fp_type[] = {
    [0x2] = {NULL, instr_fsw, NULL, "FSW"},
    [0x3] = {NULL, instr_fsd, NULL, "FSD"},
};
INIT_RISCV_INSTR_LIST(WIDTH, instr_store_fp_type);

static riscv_instr_entry instr_addw_mulw_subw_type[] = {
    [0x00] = {NULL, instr_addw, NULL, "ADDW"},
    [0x01] = {NULL, instr_mulw, NULL, "MULW"},
    [0x20] = {NULL, instr_subw, NULL, "SUBW"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_addw_mulw_subw_type);

static riscv_instr_entry instr_sllw_type[] = {
    [0x00] = {NULL, instr_sllw, NULL, "SLLW"},
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_sllw_type);

static riscv_instr_entry instr_divw_type[] = {
    [0x01] = {NULL, instr_divw, NULL, "DIVW"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_divw_type);

static riscv_instr_entry instr_srlw_divuw_sraw_type[] = {
    [0x00] = {NULL, instr_srlw, NULL, "SRLW"},
    [0x01] = {NULL, instr_divuw, NULL, "DIVUW"},
    [0x20] = {NULL, instr_sraw, NULL, "SRAW"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_srlw_divuw_sraw_type);

static riscv_instr_entry instr_remw_type[] = {
    [0x01] = {NULL, instr_remw, NULL, "REMW"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_remw_type);

static riscv_instr_entry instr_remuw_type[] = {
    [0x01] =  {NULL, instr_remuw, NULL, "REMUW"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_remuw_type);

static riscv_instr_entry instr_regw_type[] = {
    [0x0] = {NULL, NULL, &instr_addw_mulw_subw_type_list, NULL},
    [0x1] = {NULL, NULL, &instr_sllw_type_list, NULL},
    [0x4] = {NULL, NULL, &instr_divw_type_list, NULL},
    [0x5] = {NULL, NULL, &instr_srlw_divuw_sraw_type_list, NULL},
    [0x6] = {NULL, NULL, &instr_remw_type_list, NULL},
    [0x7] = {NULL, NULL, &instr_remuw_type_list, NULL},
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_regw_type);

static riscv_instr_entry instr_branch_type[] = {
    [0x0] = {NULL, instr_beq, NULL, "BEQ"},
    [0x1] = {NULL, instr_bne, NULL, "BNE"},
    [0x4] = {NULL, instr_blt, NULL, "BLT"},
    [0x5] = {NULL, instr_bge, NULL, "BGE"},
    [0x6] = {NULL, instr_bltu, NULL, "BLTU"},
    [0x7] = {NULL, instr_bgeu, NULL, "BGEU"},
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_branch_type);

static riscv_instr_entry instr_ecall_ebreak_type[] = {
    [0x0] = {NULL, instr_ecall, NULL, "ECALL"},
    [0x1] = {NULL, instr_ebreak, NULL, "EBREAK"},
};
INIT_RISCV_INSTR_LIST(RS2, instr_ecall_ebreak_type);

static riscv_instr_entry instr_sret_wfi_type[] = {
    [0x2] = {NULL, instr_sret, NULL, "SRET"},
    [0x5] = {NULL, instr_wfi, NULL, "WFI"}
};
INIT_RISCV_INSTR_LIST(RS2, instr_sret_wfi_type);

static riscv_instr_entry instr_ret_type[] = {
    [0x00] = {NULL, NULL, &instr_ecall_ebreak_type_list, NULL},
    [0x08] = {NULL, NULL, &instr_sret_wfi_type_list, NULL},
    [0x18] = {NULL, instr_mret, NULL, "MRET"},
    [0x09] = {NULL, instr_sfencevma, NULL, "SFENCEVMA"},
    [0x11] = {NULL, instr_hfencebvma, NULL, "HFENCEBVMA"},
    [0x51] = {NULL, instr_hfencegvma, NULL, "HFENCEGVMA"}
};
INIT_RISCV_INSTR_LIST(FUNC7, instr_ret_type);

static riscv_instr_entry instr_csr_type[] = {
    [0x0] = {NULL, NULL, &instr_ret_type_list, NULL},
    [0x1] = {NULL, instr_csrrw, NULL, "CSRRW"},
    [0x2] = {NULL, instr_csrrs, NULL, "CSRRS"},
    [0x3] = {NULL, instr_csrrc, NULL, "CSRRC"},
    [0x5] = {NULL, instr_csrrwi, NULL, "CSRRWI"},
    [0x6] = {NULL, instr_csrrsi, NULL, "CSRRSI"},
    [0x7] = {NULL, instr_csrrci, NULL, "CSRRCI"},
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_csr_type);

static riscv_instr_entry instr_amow_type[] = {
    [0x00] = {NULL, instr_amoaddw, NULL, "AMOADDW"},
    [0x01] = {NULL, instr_amoswapw, NULL, "AMOSWAPW"},
    [0x02] = {NULL, instr_lrw, NULL, "LRW"},
    [0x03] = {NULL, instr_scw, NULL, "SCW"},
    [0x04] = {NULL, instr_amoxorw, NULL, "AMOXORW"},
    [0x08] = {NULL, instr_amoorw, NULL, "AMOORW"},
    [0x0c] = {NULL, instr_amoandw, NULL, "AMOANDW"},
    //[0x10] = {NULL, instr_amominw, NULL},
    //[0x14] = {NULL, instr_amomaxw, NULL},
};
INIT_RISCV_INSTR_LIST(FUNC5, instr_amow_type);

static riscv_instr_entry instr_amod_type[] = {
    [0x00] = {NULL, instr_amoaddd, NULL, "AMOADDD"},
    [0x01] = {NULL, instr_amoswapd, NULL, "AMOSWAPD"},
    [0x02] = {NULL, instr_lrd, NULL, "LRD"},
    [0x03] = {NULL, instr_scd, NULL, "SCD"},
    [0x04] = {NULL, instr_amoxord, NULL, "AMOXORD"},
    [0x08] = {NULL, instr_amoord, NULL, "AMOORD"},
    [0x0c] = {NULL, instr_amoandd, NULL, "AMOANDD"},
    //[0x10] = {NULL, instr_amomind, NULL},
    //[0x14] = {NULL, instr_amomaxd, NULL},
};
INIT_RISCV_INSTR_LIST(FUNC5, instr_amod_type);

static riscv_instr_entry instr_atomic_type[] = {
    [0x2] = {NULL, NULL, &instr_amow_type_list, NULL},
    [0x3] = {NULL, NULL, &instr_amod_type_list, NULL},
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_atomic_type);

static riscv_instr_entry instr_c0_type[] = {
    [0x0] = {CIW_decode, instr_caddi4spn, NULL, "CADDI4SPN"},
    [0x2] = {CL_decode, instr_clw, NULL, "CLW"},
    [0x3] = {CL_decode, instr_cld, NULL, "CLD"},
    [0x5] = {CS_decode, instr_cfsd, NULL, "CFSD"},
    [0x6] = {CS_decode, instr_csw, NULL, "CSW"},
    [0x7] = {CS_decode, instr_csd, NULL, "CSD"}
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_c0_type);

static riscv_instr_entry instr_ca_type[] = {
    [0x0] = {NULL, instr_csub, NULL, "CSUB"},
    [0x1] = {NULL, instr_cxor, NULL, "CXOR"},
    [0x2] = {NULL, instr_cor, NULL, "COR"},
    [0x3] = {NULL, instr_cand, NULL, "CAND"},
    [0x4] = {NULL, instr_csubw, NULL, "CSUBW"},
    [0x5] = {NULL, instr_caddw, NULL, "CADDW"},
};
INIT_RISCV_INSTR_LIST(FUNC2_S, instr_ca_type);

static riscv_instr_entry instr_cb_ca_type[] = {
    [0x0] = {CB_decode, instr_csrli, NULL, "CSRLI"},
    [0x1] = {CB_decode, instr_csrai, NULL, "CSRAI"},
    [0x2] = {CB_decode, instr_candi, NULL, "CANDI"},
    [0x3] = {CA_decode, NULL, &instr_ca_type_list, NULL},
};
INIT_RISCV_INSTR_LIST(FUNC6_S, instr_cb_ca_type);

static riscv_instr_entry instr_c1_type[] = {
    [0x0] = {CI_decode, instr_caddi, NULL, "CADDI"},
    [0x1] = {CI_decode, instr_caddiw, NULL, "CADDIW"},
    [0x2] = {CI_decode, instr_cli, NULL, "CLI"},
    [0x3] = {CI_decode, instr_clui_caddi16sp, NULL, "CLUI/CADDI16SP"},
    [0x4] = {Cxx_decode, NULL, &instr_cb_ca_type_list, NULL},
    [0x5] = {CJ_decode, instr_cj, NULL, "CJ"},
    [0x6] = {CB_decode, instr_cbeqz, NULL, "CBEQZ"},
    [0x7] = {CB_decode, instr_cbnez, NULL, "CBNEZ"},
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_c1_type);

static riscv_instr_entry instr_cr_type[] = {
    [0x0] = {NULL, instr_cjr_cmv, NULL, "CJR/CMV"},
    [0x1] = {NULL, instr_cebreak_cjalr_cadd, NULL, "CEBREAK/CJALR/CADD"},
};
INIT_RISCV_INSTR_LIST(FUNC4_S, instr_cr_type);

static riscv_instr_entry instr_c2_type[] = {
    [0x0] = {CI_decode, instr_cslli, NULL, "CSLLI"},
    [0x2] = {CI_decode, instr_clwsp, NULL, "CLWSP"},
    [0x3] = {CI_decode, instr_cldsp, NULL, "CLDSP"},
    [0x4] = {CR_decode, NULL, &instr_cr_type_list, NULL},
    [0x6] = {CSS_decode, instr_cswsp, NULL, "CSWSP"},
    [0x7] = {CSS_decode, instr_csdsp, NULL, "CSDSP"},
};
INIT_RISCV_INSTR_LIST(FUNC3, instr_c2_type);

static riscv_instr_entry opcode_type[] = {
    [0x00] = {Cx_decode, NULL, &instr_c0_type_list, NULL},
    [0x01] = {Cx_decode, NULL, &instr_c1_type_list, NULL},
    [0x02] = {Cx_decode, NULL, &instr_c2_type_list, NULL},
    [0x03] = {I_decode, NULL, &instr_load_type_list, NULL},
    [0x0f] = {I_decode, NULL, &instr_fence_type_list, NULL},
    [0x13] = {I_decode, NULL, &instr_imm_type_list, NULL},
    [0x17] = {U_decode, instr_auipc, NULL, "AUIPC"},
    [0x1b] = {I_decode, NULL, &instr_immw_type_list, NULL},
    [0x23] = {S_decode, NULL, &instr_store_type_list, NULL},
    [0x27] = {FS_decode, NULL, &instr_store_fp_type_list, NULL},
    [0x2f] = {R_decode, NULL, &instr_atomic_type_list, NULL},
    [0x33] = {R_decode, NULL, &instr_reg_type_list, NULL},
    [0x37] = {U_decode, instr_lui, NULL, "LUI"},
    [0x3b] = {R_decode, NULL, &instr_regw_type_list, NULL},
    [0x63] = {B_decode, NULL, &instr_branch_type_list, NULL},
    [0x67] = {I_decode, instr_jalr, NULL, "JALR"},
    [0x6f] = {J_decode, instr_jal, NULL, "JAL"},
    [0x73] = {P_decode, NULL, &instr_csr_type_list, NULL},
};
INIT_RISCV_INSTR_LIST(OPCODE, opcode_type);
/* clang-format on */

static bool __decode(riscv_cpu *cpu, riscv_instr_desc *instr_desc)
{
    uint8_t index;

    switch (instr_desc->type.type) {
    case OPCODE:
        index = cpu->instr.opcode;
        break;
    case FUNC2_S:
        index = (cpu->instr.funct6 & 0x4) | cpu->instr.funct2;
        break;
    case FUNC3:
        index = cpu->instr.funct3;
        break;
    case FUNC4_S:
        index = cpu->instr.funct4 & 0x1;
        break;
    case FUNC5:
        index = (cpu->instr.funct7 & 0b1111100) >> 2;
        break;
    case FUNC6_S:
        index = cpu->instr.funct6 & 0x3;
        break;
    case FUNC7_S:
        index = cpu->instr.funct7 >> 1;
        break;
    case FUNC7:
        index = cpu->instr.funct7;
        break;
    case RS2:
        index = cpu->instr.rs2;
        break;
    case WIDTH:
        index = cpu->instr.width;
        break;
    default:
        ERROR("Invalid index type\n");
        /* we don't change the variable of exception number here, since
         * this should only happens when our emulator's implementation error*/
        return false;
    }

    if (index >= instr_desc->size) {
        ERROR(
            "Not implemented or invalid instruction:\n"
            "opcode = 0x%x funct3 = 0x%x funct7 = 0x%x at pc %lx\n",
            cpu->instr.opcode, cpu->instr.funct3, cpu->instr.funct7,
            cpu->pc - 4);
        cpu->exc.exception = IllegalInstruction;
        return false;
    }

    riscv_instr_entry entry = instr_desc->instr_list[index];

    if (entry.decode_func)
        entry.decode_func(&cpu->instr);

    if (entry.decode_func == NULL && entry.exec_func == NULL &&
        entry.next == NULL) {
        ERROR(
            "@ Not implemented or invalid instruction:\n"
            "opcode = 0x%x funct3 = 0x%x funct7 = 0x%x at pc %lx\n",
            cpu->instr.opcode, cpu->instr.funct3, cpu->instr.funct7,
            cpu->pc - 4);
        cpu->exc.exception = IllegalInstruction;
        return false;
    }

    if (entry.next != NULL)
        return __decode(cpu, entry.next);
    else
        cpu->instr.exec_func = entry.exec_func;

    if (entry.entry_name) {
        LOG_DEBUG("[DEBUG] next INSTR: %s\n", entry.entry_name);
    }

    return true;
}

static uint64_t addr_translate(riscv_cpu *cpu, uint64_t addr, Access access)
{
    uint64_t satp = read_csr(&cpu->csr, SATP);
    // if not enable page table translation
    if (satp >> 60 != 8)
        return addr;

    /*  When MPRV=0, translation and protection behave as normal.
     *  Whem MPRV=1, load and store memory addresses are translated
     *  and protected as though the current privilege mode were set to MPP */
    if (cpu->mode.mode == MACHINE)
        if ((access == Access_Instr) ||
            (!check_csr_bit(&cpu->csr, MSTATUS, MSTATUS_MPRV)) ||
            ((read_csr(&cpu->csr, MSTATUS) >> 11) == MACHINE))
            return addr;

    /* Reference to:
     * - 4.3.2 Virtual Address Translation Process
     * - 4.4 Sv39: Page-Based 39-bit Virtual-Memory System */

    // the format of SV39 virtual address
    uint64_t vpn[3] = {(addr >> 12) & 0x1ff, (addr >> 21) & 0x1ff,
                       (addr >> 30) & 0x1ff};

    /* 1. Let a be satp.ppn × PAGESIZE, and let i = LEVELS − 1. */
    uint64_t a = (satp & SATP_PPN) << PAGE_SHIFT;
    int i = LEVELS - 1;


    sv39_pte_t pte;
    while (1) {
        /* 2. Let pte be the value of the PTE at address a+va.vpn[i]×PTESIZE. */
        uint64_t tmp = read_bus(&cpu->bus, a + vpn[i] * 8, 64, &cpu->exc);
        pte = sv39_pte_new(tmp);

        if (cpu->exc.exception != NoException)
            return -1;

        /* 3. If pte.v = 0, or if pte.r = 0 and pte.w = 1, stop and raise a
         * page-fault exception corresponding to the original access type */
        if (pte.v == 0 || (pte.r == 0 && pte.w == 1))
            goto translate_fail;

        /* 4.
         *
         * Otherwise, the PTE is valid.
         *
         * If pte.r = 1 or pte.x = 1, go to step 5.
         *
         * Otherwise, this PTE is a pointer to the next level of the page table.
         * Let i = i − 1. If i < 0, stop and raise a page-fault exception
         * corresponding to the original access type.
         *
         * Otherwise, let a = pte.ppn × PAGESIZE and go to step 2. */
        if (pte.r == 1 || pte.x == 1)
            break;

        i--;
        if (i < 0)
            goto translate_fail;

        a = pte.ppn << PAGE_SHIFT;
    }


    /* 5. (skip) A leaf PTE has been found. Determine if the requested memory
     * access is allowed by the pte.r, pte.w, pte.x, and pte.u bits, given the
     * current privilege mode and the value of the SUM and MXR fields of the
     * mstatus register. If not, stop and raise a page-fault exception
     * corresponding to the original access type. */
    switch (access) {
    case Access_Instr:
        if (pte.x == 0)
            goto translate_fail;
        break;
    case Access_Load:
        if (pte.r == 0)
            goto translate_fail;
        break;
    case Access_Store:
        if (pte.w == 0)
            goto translate_fail;
        break;
    }

    /* 6. If i > 0 and pte.ppn[i − 1 : 0] != 0, this is a misaligned superpage;
     * stop and raise a page-fault exception corresponding to the original
     * access type. */
    uint64_t ppn[3] = {pte.ppn & 0x1ff, (pte.ppn >> 9) & 0x1ff,
                       (pte.ppn >> 18) & 0x3ffffff};

    if (i > 0) {
        for (int idx = i - 1; idx > 0; idx--) {
            if (ppn[idx] != 0)
                goto translate_fail;
        }
    }

    /* 7. (Skip) If pte.a = 0, or if the memory access is a store and pte.d = 0,
     * raise a page-fault exception corresponding to the original access type */

    /* 8. The translation is successful. The translated physical address is
     * given as follows:
     * - pa.pgoff = va.pgoff.
     * - If i > 0, then this is a superpage translation and
     *   pa.ppn[i − 1 : 0] = va.vpn[i − 1 : 0].
     * - pa.ppn[LEVELS − 1 : i] = pte.ppn[LEVELS − 1 : i]. */

    int fix = 0;
    while (i > 0) {
        ppn[fix] = vpn[fix];
        fix++;
        i--;
    }

    return ppn[2] << 30 | ppn[1] << 21 | ppn[0] << 12 | (addr & 0xfff);

translate_fail:
    switch (access) {
    case Access_Instr:
        cpu->exc.exception = InstructionPageFault;
        break;
    case Access_Load:
        cpu->exc.exception = LoadPageFault;
        break;
    case Access_Store:
        cpu->exc.exception = StoreAMOPageFault;
        break;
    }
    cpu->exc.value = addr;
    return -1;
}

static Trap handle_exception(riscv_cpu *cpu, uint64_t exc_pc)
{
    riscv_mode prev_mode = cpu->mode;
    uint8_t cause = cpu->exc.exception;
    uint64_t medeleg = read_csr(&cpu->csr, MEDELEG);

    // TODO: support handling for user mode
    if (((medeleg >> cause) & 1) == 0)
        cpu->mode.mode = MACHINE;
    else {
        uint64_t sedeleg = read_csr(&cpu->csr, SEDELEG);
        if (((sedeleg >> cause) & 1) == 0)
            cpu->mode.mode = SUPERVISOR;
        else
            cpu->mode.mode = USER;
    }

    if (cpu->mode.mode == SUPERVISOR) {
        /* The last two bits of stvec indicate the vector mode, which make
         * different for pc setting when interrupt. For (synchronous) exception,
         * no matter what the mode is, always set pc to BASE by stvec when
         * exception. */
        cpu->pc = read_csr(&cpu->csr, STVEC) & ~0x3;
        /* The low bit of sepc(sepc[0]) is always zero. When a trap is taken
         * into S-mode, sepc is written with the virtual address of the
         * instruction that was interrupted or that encountered the exception.
         */
        write_csr(&cpu->csr, SEPC, exc_pc & ~0x1);
        /* When a trap is taken into S-mode, scause is written with a code
         * indicating the event that caused the trap. */
        write_csr(&cpu->csr, SCAUSE, cause);
        /* FIXME: only some case of exception set this value correctly */
        write_csr(&cpu->csr, STVAL, cpu->exc.value);

        /* When a trap is taken into supervisor mode, SPIE is set to SIE */
        uint64_t sstatus = read_csr(&cpu->csr, SSTATUS);
        write_csr(&cpu->csr, SSTATUS,
                  (sstatus & ~SSTATUS_SPIE) | ((sstatus & SSTATUS_SIE) << 4));
        /* SIE is set to 0 */
        clear_csr_bits(&cpu->csr, SSTATUS, SSTATUS_SIE);
        /* When a trap is taken, SPP is set to 0 if the trap originated from
         * user mode, or 1 otherwise. */
        sstatus = read_csr(&cpu->csr, SSTATUS);
        write_csr(&cpu->csr, SSTATUS,
                  (sstatus & ~SSTATUS_SPP) | prev_mode.mode << 8);
    } else if (cpu->mode.mode == MACHINE) {
        /* Handle in machine mode. You can see that the process is similar to
         * handle in supervisor mode. */
        cpu->pc = read_csr(&cpu->csr, MTVEC) & ~0x3;
        write_csr(&cpu->csr, MEPC, exc_pc & ~0x1);
        write_csr(&cpu->csr, MCAUSE, cause);
        /* FIXME: only some case of exception set this value correctly */
        write_csr(&cpu->csr, MTVAL, cpu->exc.value);
        uint64_t mstatus = read_csr(&cpu->csr, MSTATUS);
        write_csr(&cpu->csr, MSTATUS,
                  (mstatus & ~MSTATUS_MPIE) | ((mstatus & MSTATUS_MIE) << 4));
        clear_csr_bits(&cpu->csr, MSTATUS, MSTATUS_MIE);
        mstatus = read_csr(&cpu->csr, MSTATUS);
        write_csr(&cpu->csr, MSTATUS,
                  (mstatus & ~MSTATUS_MPP) | prev_mode.mode << 11);
    } else {
        ERROR("Taking trap in user mode is not supported!\n");
        exit(1);
    }

    // https://github.com/d0iasm/rvemu/blob/master/src/exception.rs
    switch (cpu->exc.exception) {
    case InstructionAddressMisaligned:
    case InstructionAccessFault:
        return Trap_Fatal;
    case IllegalInstruction:
        return Trap_Fatal;  // stop the emulator if overcome illegal instruction
    case Breakpoint:
        return Trap_Requested;
    case LoadAddressMisaligned:
    case LoadAccessFault:
    case StoreAMOAddressMisaligned:
    case StoreAMOAccessFault:
        return Trap_Fatal;
    case EnvironmentCallFromUMode:
    case EnvironmentCallFromSMode:
    case EnvironmentCallFromMMode:
        return Trap_Requested;
    case InstructionPageFault:
    case LoadPageFault:
    case StoreAMOPageFault:
        return Trap_Invisible;
    default:
        ERROR("Not defined exception %d\n", cpu->exc.exception);
        return Trap_Fatal;
    }
}

static void interrput_take_trap(riscv_cpu *cpu, riscv_mode new_mode)
{
    uint64_t irq_pc = cpu->pc;
    riscv_mode prev_mode = cpu->mode;
    uint8_t cause = cpu->irq.irq;

    cpu->mode = new_mode;

    if (cpu->mode.mode == SUPERVISOR) {
        uint64_t stvec = read_csr(&cpu->csr, STVEC);
        if (stvec & 0x1)
            cpu->pc = (stvec & ~0x3) + 4 * cause;
        else
            cpu->pc = stvec & ~0x3;

        write_csr(&cpu->csr, SEPC, irq_pc & ~0x1);
        /* The Interrupt bit in the scause register is set if the trap was
         * caused by an interrupt */
        write_csr(&cpu->csr, SCAUSE, 1UL << 63 | cause);
        /* FIXME: only some case of interrupt set this value correctly */
        write_csr(&cpu->csr, STVAL, cpu->irq.value);

        uint64_t sstatus = read_csr(&cpu->csr, SSTATUS);
        write_csr(&cpu->csr, SSTATUS,
                  (sstatus & ~SSTATUS_SPIE) | ((sstatus & SSTATUS_SIE) << 4));
        clear_csr_bits(&cpu->csr, SSTATUS, SSTATUS_SIE);
        sstatus = read_csr(&cpu->csr, SSTATUS);
        write_csr(&cpu->csr, SSTATUS,
                  (sstatus & ~SSTATUS_SPP) | prev_mode.mode << 8);
    } else if (cpu->mode.mode == MACHINE) {
        uint64_t mtvec = read_csr(&cpu->csr, MTVEC);
        if (mtvec & 0x1)
            cpu->pc = (mtvec & ~0x3) + 4 * cause;
        else
            cpu->pc = mtvec & ~0x3;

        write_csr(&cpu->csr, MEPC, irq_pc & ~0x1);
        write_csr(&cpu->csr, MCAUSE, 1UL << 63 | cause);
        /* FIXME: only some case of interrupt set this value correctly */
        write_csr(&cpu->csr, MTVAL, cpu->irq.value);
        uint64_t mstatus = read_csr(&cpu->csr, MSTATUS);
        write_csr(&cpu->csr, MSTATUS,
                  (mstatus & ~MSTATUS_MPIE) | ((mstatus & MSTATUS_MIE) << 4));
        clear_csr_bits(&cpu->csr, MSTATUS, MSTATUS_MIE);
        mstatus = read_csr(&cpu->csr, MSTATUS);
        write_csr(&cpu->csr, MSTATUS,
                  (mstatus & ~MSTATUS_MPP) | prev_mode.mode << 11);
    } else {
        ERROR("Taking trap in user mode is not supported!\n");
        exit(1);
    }
}

static bool irq_enable(riscv_cpu *cpu, uint8_t cause)
{
    uint64_t mideleg = read_csr(&cpu->csr, MIDELEG);
    riscv_mode new_mode;

    // TODO: support handling for user mode
    if (((mideleg >> cause) & 1) == 0)
        new_mode.mode = MACHINE;
    else {
        uint64_t sideleg = read_csr(&cpu->csr, SIDELEG);
        if (((sideleg >> cause) & 1) == 0)
            new_mode.mode = SUPERVISOR;
        else
            new_mode.mode = USER;
    }

    if (new_mode.mode < cpu->mode.mode)
        return false;
    else if (new_mode.mode == cpu->mode.mode) {
        if (cpu->mode.mode == MACHINE) {
            if (!check_csr_bit(&cpu->csr, MSTATUS, MSTATUS_MIE))
                return false;
        } else if (cpu->mode.mode == SUPERVISOR) {
            if (!check_csr_bit(&cpu->csr, SSTATUS, SSTATUS_SIE)) {
                return false;
            }
        } else {
            ERROR("Handling interrupt in USER mode is not supported!\n");
            exit(1);
        }
    }

    cpu->irq.irq = cause;
    cpu->irq.value = cpu->pc;
    interrput_take_trap(cpu, new_mode);
#ifdef ICACHE_CONFIG
    // flush cache when jumping in trap handler
    invalid_icache(&cpu->icache);
#endif
    return true;
}

static void handle_interrupt(riscv_cpu *cpu)
{
    /* FIXME:
     * 1. Priority of interrupt should be considered
     * 2. The interactive between PLIC and interrput are not fully implement, I
     * should dig in more to perform better emulation.
     */

    uint64_t pending = read_csr(&cpu->csr, MIE) & read_csr(&cpu->csr, MIP);

    if (pending & MIP_MEIP) {
        if (irq_enable(cpu, MachineExternalInterrupt)) {
            clear_csr_bits(&cpu->csr, MIP, MIP_MEIP);
            return;
        }
    }
    if (pending & MIP_MSIP) {
        if (irq_enable(cpu, MachineSoftwareInterrupt)) {
            clear_csr_bits(&cpu->csr, MIP, MIP_MSIP);
            return;
        }
    }
    if (pending & MIP_MTIP) {
        if (irq_enable(cpu, MachineTimerInterrupt)) {
            clear_csr_bits(&cpu->csr, MIP, MIP_MTIP);
            return;
        }
    }


    /* FIXME: do we need to update this since SIP and SIE are subset of the
     * equivalent machine-mode CSR? */
    pending = read_csr(&cpu->csr, SIE) & read_csr(&cpu->csr, SIP);

    if (pending & MIP_SEIP) {
        if (irq_enable(cpu, SupervisorExternalInterrupt)) {
            clear_csr_bits(&cpu->csr, MIP, MIP_SEIP);
            return;
        }
    }
    if (pending & MIP_SSIP) {
        if (irq_enable(cpu, SupervisorSoftwareInterrupt)) {
            clear_csr_bits(&cpu->csr, MIP, MIP_SSIP);
            return;
        }
    }
    if (pending & MIP_STIP) {
        if (irq_enable(cpu, SupervisorTimerInterrupt)) {
            clear_csr_bits(&cpu->csr, MIP, MIP_STIP);
            return;
        }
    }
    /* If all of our implementation are right, we don't actually need to
     * clean up the irq structure below. But for easily debugging purpose, we'll
     * reset all of the members in irq structure now. */
#ifdef DEBUG
    cpu->irq.irq = NoInterrupt;
    cpu->irq.value = 0;
#endif
}

/* these two functions are the indirect layer of read / write bus from cpu,
 * which will do address translation before actually read / write the bus */
static uint64_t read_cpu(riscv_cpu *cpu, uint64_t addr, uint8_t size)
{
    addr = addr_translate(cpu, addr, Access_Load);
    if (cpu->exc.exception != NoException)
        return -1;
    return read_bus(&cpu->bus, addr, size, &cpu->exc);
}

static bool write_cpu(riscv_cpu *cpu,
                      uint64_t addr,
                      uint8_t size,
                      uint64_t value)
{
    addr = addr_translate(cpu, addr, Access_Store);
    if (cpu->exc.exception != NoException)
        return false;
    return write_bus(&cpu->bus, addr, size, value, &cpu->exc);
}

bool init_cpu(riscv_cpu *cpu, const char *filename, const char *rfs_name)
{
    if (!init_bus(&cpu->bus, filename, rfs_name))
        return false;

    if (!init_csr(&cpu->csr))
        return false;

#ifdef ICACHE_CONFIG
    if (!init_icache(&cpu->icache))
        return false;
#endif

    cpu->mode.mode = MACHINE;
    cpu->exc.exception = NoException;
    cpu->irq.irq = NoInterrupt;

    memset(&cpu->instr, 0, sizeof(riscv_instr));
    memset(&cpu->xreg[0], 0, sizeof(uint64_t) * 32);
    for (int i = 0; i < 32; i++) {
        cpu->freg[i].u = 0;
        cpu->freg[i].f = 0;
    }

    cpu->pc = BOOT_ROM_BASE;
    cpu->xreg[2] = DRAM_BASE + DRAM_SIZE;
    cpu->instr.exec_func = NULL;
    return true;
}

#ifdef ICACHE_CONFIG
static bool fetch_icache(riscv_cpu *cpu)
{
    riscv_instr *icache_instr = read_icache(&cpu->icache, cpu->pc);

    if (icache_instr != NULL) {
        memcpy(&cpu->instr, icache_instr, sizeof(riscv_instr));
        cpu->pc += (cpu->instr.instr & 0x3) == 0x3 ? 4 : 2;

        LOG_DEBUG("[DEBUG] cache hit \n");

        LOG_DEBUG(
            "[DEBUG] instr: 0x%x\n"
            "opcode = 0x%x funct3 = 0x%x funct7 = 0x%x rs2 = 0x%x\n",
            cpu->instr.instr, cpu->instr.opcode, cpu->instr.funct3,
            cpu->instr.funct7, cpu->instr.rs2);

        return true;
    }

    return false;
}
#endif

static bool fetch(riscv_cpu *cpu)
{
    uint64_t pc = addr_translate(cpu, cpu->pc, Access_Instr);
    if (cpu->exc.exception != NoException)
        return false;

    uint32_t instr = read_bus(&cpu->bus, pc, 32, &cpu->exc);
    if (cpu->exc.exception != NoException)
        return false;

    int pc_shift = 0;

    if ((instr & 0x3) != 0x3) {
        pc_shift = 2;

        instr &= 0xffff;

        if (instr == 0) {
            cpu->exc.exception = IllegalInstruction;
            return false;
        }

        cpu->instr.instr = instr;
        cpu->instr.opcode = instr & 0x3;
    } else {
        pc_shift = 4;

        cpu->instr.instr = instr;
        cpu->instr.opcode = instr & 0x7f;
    }

    LOG_DEBUG("[DEBUG] pc: %lx instr: 0x%x\n", pc, cpu->instr.instr);

    cpu->pc += pc_shift;
    return true;
}

static bool decode(riscv_cpu *cpu)
{
    bool ret = __decode(cpu, &opcode_type_list);

    LOG_DEBUG(
        "[DEBUG] instr: 0x%x opcode = 0x%x funct3 = 0x%x funct7 = 0x%x\n"
        "        rs1 = 0x%x rs2 = 0x%x rd = 0x%x\n",
        cpu->instr.instr, cpu->instr.opcode, cpu->instr.funct3,
        cpu->instr.funct7, cpu->instr.rs1, cpu->instr.rs2, cpu->instr.rd);

#ifdef ICACHE_CONFIG
    // cache the decoding result
    if (ret) {
        if ((cpu->instr.instr & 0x3) != 0x3)
            write_icache(&cpu->icache, cpu->pc - 2, cpu->instr);
        else
            write_icache(&cpu->icache, cpu->pc - 4, cpu->instr);
    }
#endif

    return ret;
}

static bool exec(riscv_cpu *cpu)
{
    cpu->instr.exec_func(cpu);

    // Emulate register x0 to 0
    cpu->xreg[0] = 0;

    if (cpu->exc.exception != NoException)
        return false;

        /* If all of our implementation are right, we don't actually need to
         * clean up the structure below. But for easily debugging purpose, we'll
         * reset all of the instruction-relatd structure now. */
#ifdef DEBUG
    memset(&cpu->instr, 0, sizeof(riscv_instr));
    cpu->instr.exec_func = NULL;
#endif
    return true;
}

static void dump_reg(riscv_cpu *cpu)
{
    static char *abi_name[] = {
        "z",  "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
        "a1", "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
        "s6", "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};

    printf("pc = 0x%lx\n", cpu->pc);
    for (size_t i = 0; i < 32; i++) {
        printf("x%-2ld(%-3s) = 0x%-16lx, ", i, abi_name[i], cpu->xreg[i]);
        if (!((i + 1) & 3))
            printf("\n");
    }
    printf("\n");
}

static void dump_csr(riscv_cpu *cpu)
{
    printf("%-10s = 0x%-16lx, ", "MSTATUS", read_csr(&cpu->csr, MSTATUS));
    printf("%-10s = 0x%-16lx, ", "MTVEC", read_csr(&cpu->csr, MTVEC));
    printf("%-10s = 0x%-16lx, ", "MEPC", read_csr(&cpu->csr, MEPC));
    printf("%-10s = 0x%-16lx\n", "MCAUSE", read_csr(&cpu->csr, MCAUSE));

    printf("%-10s = 0x%-16lx, ", "SSTATUS", read_csr(&cpu->csr, SSTATUS));
    printf("%-10s = 0x%-16lx, ", "STVEC", read_csr(&cpu->csr, STVEC));
    printf("%-10s = 0x%-16lx, ", "SEPC", read_csr(&cpu->csr, SEPC));
    printf("%-10s = 0x%-16lx\n", "SCAUSE", read_csr(&cpu->csr, SCAUSE));
}

bool tick_cpu(riscv_cpu *cpu)
{
    // TODO: sync mtime in Clint and TIME in CSR
    // Increment the value for Time in CSR
    tick_csr(&cpu->csr);
    // Increment the value for mtime in Clint
    tick_bus(&cpu->bus, &cpu->csr);
    handle_interrupt(cpu);

    uint64_t instr_addr = cpu->pc;
    bool ret = true;

#ifdef ICACHE_CONFIG
    if (!fetch_icache(cpu))
#endif
    {
        ret = fetch(cpu);
        if (!ret)
            goto get_trap;

        ret = decode(cpu);
        if (!ret)
            goto get_trap;
    }

    ret = exec(cpu);
get_trap:
    if (!ret) {
        uint64_t next_pc = cpu->pc;
        Trap trap = handle_exception(cpu, instr_addr);
        if (trap == Trap_Fatal) {
            dump_reg(cpu);
            dump_csr(cpu);
            ERROR("CPU mode: %d, exception %x happen before pc %lx\n",
                  cpu->mode.mode, cpu->exc.exception, next_pc);
            return false;
        }
        // reset exception flag if recovery from trap
        cpu->exc.exception = NoException;
#ifdef ICACHE_CONFIG
        // flush cache when jumping in trap handler
        invalid_icache(&cpu->icache);
#endif
    }

    return true;
}

void free_cpu(riscv_cpu *cpu)
{
    free_bus(&cpu->bus);
#ifdef ICACHE_CONFIG
    free_icache(&cpu->icache);
#endif
}
