#include "cpu.h"

// MISC. //

static void apply_page_boundary_penalty(CPUState *cpu, uint16_t a, uint16_t b) {
    if ((a << 4) != (b << 4)) {
        (cpu->t)++;
    }
}

// P.STATUS REGISTER //

static bool get_p_flag(CPUState *cpu, int flag) {
    return cpu->p & (1 << flag);
}

static void set_p_flag(CPUState *cpu, int flag, bool value) {
    if (value) {
        cpu->p |= (1 << flag);
    } else {
        cpu->p &= ~(1 << flag);
    }
}

static void apply_p_nz(CPUState *cpu, uint8_t value) {
    set_p_flag(cpu, P_Z, !value);
    set_p_flag(cpu, P_N, value & (1 << 7));
}

// STACK REGISTER //

static uint16_t get_stack_addr(CPUState *cpu) {
    return 0x100 + (uint16_t)(cpu->s);
}

static void stack_push(CPUState *cpu, uint8_t value) {
    mm_write(cpu->mm, get_stack_addr(cpu), value);
    (cpu->s)--;
}
static void stack_push_word(CPUState *cpu, uint16_t value) {
    stack_push(cpu, value & 0xff);
    stack_push(cpu, value >> 8);
}

static uint8_t stack_pull(CPUState *cpu) {
    (cpu->s)++;
    return mm_read(cpu->mm, get_stack_addr(cpu));
}
static uint16_t stack_pull_word(CPUState *cpu) {
    uint16_t value = (uint16_t)stack_pull(cpu) << 8;
    return value + (uint16_t)stack_pull(cpu);
}

// INTERRUPT HANDLING //

static int interrupt(CPUState *cpu, bool b_flag, uint16_t ivt_addr) {
    set_p_flag(cpu, P_B, b_flag);
    set_p_flag(cpu, P_I, true);
    if (ivt_addr != IVT_RESET) {
        stack_push_word(cpu, cpu->pc);
        stack_push(cpu, cpu->p);
    }
    cpu->pc = mm_read_word(cpu->mm, ivt_addr);
    return 7;
}

// OPCODES //

static uint8_t get_param_value(CPUState *cpu, const Opcode *op, OpParam param) {
    if (op->am == AM_IMMEDIATE) {
        return param.immediate_value[0];
    }
    return mm_read(cpu->mm, param.addr);
}

static void op_T(CPUState *cpu, const Opcode *op, OpParam param) {
    *op->reg2 = *op->reg1;
    if (op->reg2 != &cpu->s) {
        apply_p_nz(cpu, *op->reg2);
    }
}

static void op_LD(CPUState *cpu, const Opcode *op, OpParam param) {
    *op->reg1 = get_param_value(cpu, op, param);
    apply_p_nz(cpu, *op->reg1);
}

static void op_ST(CPUState *cpu, const Opcode *op, OpParam param) {
    mm_write(cpu->mm, param.addr, *op->reg1);
}

static void op_PH(CPUState *cpu, const Opcode *op, OpParam param) {
    uint8_t value = *op->reg1;
    if (op->reg1 == &cpu->p) {
        value |= (1 << P_B) + (1 << P__);
    }
    stack_push(cpu, value);
}

static void op_PL(CPUState *cpu, const Opcode *op, OpParam param) {
    *op->reg1 = stack_pull(cpu);
    if (op->reg1 == &cpu->p) {
        *op->reg1 &= ~((1 << P_B) + (1 << P__));
    } else {
        apply_p_nz(cpu, *op->reg1);
    }
}

static void op_ADC(CPUState *cpu, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(cpu, op, param);
    uint8_t carry = (get_p_flag(cpu, P_C) ? 1 : 0);
    set_p_flag(cpu, P_C, ((int)(cpu->a) + (int)carry + (int)value) >= 0x100);
    uint8_t result = cpu->a + carry + value;
    set_p_flag(cpu, P_V, (result & (1 << 7)) != (cpu->a & (1 << 7)));
    cpu->a = result;
    apply_p_nz(cpu, cpu->a);
}

static void op_SBC(CPUState *cpu, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(cpu, op, param);
    uint8_t carry = (get_p_flag(cpu, P_C) ? 1 : 0);
    set_p_flag(cpu, P_C, ((int)(cpu->a) + (int)carry - 1 - (int)value) >= 0);
    uint8_t result = cpu->a + carry - 1 - value;
    set_p_flag(cpu, P_V, (result & (1 << 7)) != (cpu->a & (1 << 7)));
    cpu->a = result;
    apply_p_nz(cpu, cpu->a);
}

static void op_AND(CPUState *cpu, const Opcode *op, OpParam param) {
    cpu->a &= get_param_value(cpu, op, param);
    apply_p_nz(cpu, cpu->a);
}

static void op_EOR(CPUState *cpu, const Opcode *op, OpParam param) {
    cpu->a ^= get_param_value(cpu, op, param);
    apply_p_nz(cpu, cpu->a);
}

static void op_ORA(CPUState *cpu, const Opcode *op, OpParam param) {
    cpu->a |= get_param_value(cpu, op, param);
    apply_p_nz(cpu, cpu->a);
}

static void op_CMP(CPUState *cpu, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(cpu, op, param);
    set_p_flag(cpu, P_C, ((int)(*op->reg1) - (int)value) >= 0);
    apply_p_nz(cpu, *op->reg1 - value);
}

static void op_BIT(CPUState *cpu, const Opcode *op, OpParam param) {
    uint8_t value = get_param_value(cpu, op, param);
    set_p_flag(cpu, P_Z, !(cpu->a & value));
    set_p_flag(cpu, P_N, value & (1 << 7));
    set_p_flag(cpu, P_V, value & (1 << 6));
}

static void op_INC(CPUState *cpu, const Opcode *op, OpParam param) {
    uint8_t result = get_param_value(cpu, op, param) + 1;
    mm_write(cpu->mm, param.addr, result);
    apply_p_nz(cpu, result);
}

static void op_IN(CPUState *cpu, const Opcode *op, OpParam param) {
    apply_p_nz(cpu, ++(*op->reg1));
}

static void op_DEC(CPUState *cpu, const Opcode *op, OpParam param) {
    uint8_t result = get_param_value(cpu, op, param) - 1;
    mm_write(cpu->mm, param.addr, result);
    apply_p_nz(cpu, result);
}

static void op_DE(CPUState *cpu, const Opcode *op, OpParam param) {
    apply_p_nz(cpu, --(*op->reg1));
}

static void shift_left(CPUState *cpu, const Opcode *op, OpParam param, uint8_t carry) {
    if (op->reg1) {
        set_p_flag(cpu, P_C, *op->reg1 & (1 << 7));
        *op->reg1 <<= 1;
        *op->reg1 += carry;
        apply_p_nz(cpu, *op->reg1);
    } else {
        uint8_t value = get_param_value(cpu, op, param);
        set_p_flag(cpu, P_C, value & (1 << 7));
        value <<= 1;
        value += carry;
        mm_write(cpu->mm, param.addr, value);
        apply_p_nz(cpu, value);
    }
}
static void op_ASL(CPUState *cpu, const Opcode *op, OpParam param) {
    shift_left(cpu, op, param, 0);
}
static void op_ROL(CPUState *cpu, const Opcode *op, OpParam param) {
    shift_left(cpu, op, param, (get_p_flag(cpu, P_C) ? 1 : 0));
}

static void shift_right(CPUState *cpu, const Opcode *op, OpParam param, uint8_t carry) {
    if (op->reg1) {
        set_p_flag(cpu, P_C, *op->reg1 & 1);
        *op->reg1 >>= 1;
        *op->reg1 += carry;
        apply_p_nz(cpu, *op->reg1);
    } else {
        uint8_t value = get_param_value(cpu, op, param);
        set_p_flag(cpu, P_C, value & 1);
        value >>= 1;
        value += carry;
        mm_write(cpu->mm, param.addr, value);
        apply_p_nz(cpu, value);
    }
}
static void op_LSR(CPUState *cpu, const Opcode *op, OpParam param) {
    shift_right(cpu, op, param, 0);
}
static void op_ROR(CPUState *cpu, const Opcode *op, OpParam param) {
    shift_right(cpu, op, param, (get_p_flag(cpu, P_C) ? 1 << 7 : 0));
}

static void op_JMP(CPUState *cpu, const Opcode *op, OpParam param) {
    cpu->pc = param.addr;
}

static void op_JSR(CPUState *cpu, const Opcode *op, OpParam param) {
    stack_push_word(cpu, cpu->pc);
    cpu->pc = param.addr;
}

static void op_RTI(CPUState *cpu, const Opcode *op, OpParam param) {
    cpu->s = stack_pull(cpu) & ~((1 << P_B) + (1 << P__));
    cpu->pc = stack_pull_word(cpu);
}

static void op_RTS(CPUState *cpu, const Opcode *op, OpParam param) {
    cpu->pc = stack_pull_word(cpu); // + 1 ??
}

static void cond_branch(CPUState *cpu, OpParam param, int flag, bool value) {
    if (get_p_flag(cpu, flag) != value) {
        return;
    }
    cpu->t++;
    uint16_t new_pc = cpu->pc + param.relative_addr[0];
    apply_page_boundary_penalty(cpu, cpu->pc, new_pc);
    cpu->pc = new_pc;
}
static void op_BPL(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_N, false);
}
static void op_BMI(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_N, true);
}
static void op_BVC(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_V, false);
}
static void op_BVS(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_V, true);
}
static void op_BCC(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_C, false);
}
static void op_BCS(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_C, true);
}
static void op_BNE(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_Z, false);
}
static void op_BEQ(CPUState *cpu, const Opcode *op, OpParam param) {
    cond_branch(cpu, param, P_Z, true);
}

static void op_BRK(CPUState *cpu, const Opcode *op, OpParam param) {
    cpu->pc++;
    cpu->t += interrupt(cpu, true, IVT_IRQ);
}

static void op_CLC(CPUState *cpu, const Opcode *op, OpParam param) {
    set_p_flag(cpu, P_C, false);
}
static void op_CLI(CPUState *cpu, const Opcode *op, OpParam param) {
    set_p_flag(cpu, P_I, false);
}
static void op_CLD(CPUState *cpu, const Opcode *op, OpParam param) {
    set_p_flag(cpu, P_D, false);
}
static void op_CLV(CPUState *cpu, const Opcode *op, OpParam param) {
    set_p_flag(cpu, P_V, false);
}
static void op_SEC(CPUState *cpu, const Opcode *op, OpParam param) {
    set_p_flag(cpu, P_C, true);
}
static void op_SEI(CPUState *cpu, const Opcode *op, OpParam param) {
    set_p_flag(cpu, P_I, true);
}
static void op_SED(CPUState *cpu, const Opcode *op, OpParam param) {
    set_p_flag(cpu, P_D, true);
}

// DEBUG //

static void cpu_debug_print_state(CPUState *cpu) {
    printf("PC=%04x A=%02x X=%02x Y=%02x P=%02x[",
           cpu->pc, cpu->a, cpu->x, cpu->y, cpu->p);
    for (int i = 0; i < 8; i++) {
        printf("%c", (cpu->p & (1 << i) ? "czidb-vn"[i] : '.'));
    }
    printf("] S=%02x{", cpu->s);
    for (int i = 0xff; i > cpu->s; i--) {
        printf(" %02x", cpu->mm->wram[0x100 + i]);
    }
    printf(" }\n");
}

// PUBLIC FUNCTIONS //

void cpu_init(CPUState *cpu, MemoryMap *mm) {
    cpu->a = cpu->x = cpu->y = 0;
    cpu->s = 0xff;
    cpu->p = 1 << P__;
    cpu->pc = 0;
    cpu->t = 0;
    
    cpu->mm = mm;
    
    // Initialize name on all opcodes so we can detect illegal usage
    for (int i = 0; i < 0x100; i++) {
        cpu->opcodes[i].name = NULL;
    }
    
    uint8_t *a = &cpu->a;
    uint8_t *x = &cpu->x;
    uint8_t *y = &cpu->y;
    uint8_t *s = &cpu->s;
    uint8_t *p = &cpu->p;
    
    // Define all legal opcodes
    cpu->opcodes[0xA8] = (Opcode) {"TAY", a, y, 2, op_T, AM_IMPLIED};
    cpu->opcodes[0xAA] = (Opcode) {"TAX", a, x, 2, op_T, AM_IMPLIED};
    cpu->opcodes[0xBA] = (Opcode) {"TSX", s, x, 2, op_T, AM_IMPLIED};
    cpu->opcodes[0x98] = (Opcode) {"TYA", y, a, 2, op_T, AM_IMPLIED};
    cpu->opcodes[0x8A] = (Opcode) {"TXA", x, a, 2, op_T, AM_IMPLIED};
    cpu->opcodes[0x9A] = (Opcode) {"TXS", x, s, 2, op_T, AM_IMPLIED};
    cpu->opcodes[0xA9] = (Opcode) {"LDA", a, 0, 2, op_LD, AM_IMMEDIATE};
    cpu->opcodes[0xA2] = (Opcode) {"LDX", x, 0, 2, op_LD, AM_IMMEDIATE};
    cpu->opcodes[0xA0] = (Opcode) {"LDY", y, 0, 2, op_LD, AM_IMMEDIATE};
    
    cpu->opcodes[0xA5] = (Opcode) {"LDA", a, 0, 3, op_LD, AM_ZP};
    cpu->opcodes[0xB5] = (Opcode) {"LDA", a, x, 4, op_LD, AM_ZP};
    cpu->opcodes[0xAD] = (Opcode) {"LDA", a, 0, 4, op_LD, AM_ABSOLUTE};
    cpu->opcodes[0xBD] = (Opcode) {"LDA", a, x, -4, op_LD, AM_ABSOLUTE};
    cpu->opcodes[0xB9] = (Opcode) {"LDA", a, y, -4, op_LD, AM_ABSOLUTE};
    cpu->opcodes[0xA1] = (Opcode) {"LDA", a, 0, 6, op_LD, AM_INDIRECT_X};
    cpu->opcodes[0xB1] = (Opcode) {"LDA", a, 0, -5, op_LD, AM_INDIRECT_Y};
    cpu->opcodes[0xA6] = (Opcode) {"LDX", x, 0, 3, op_LD, AM_ZP};
    cpu->opcodes[0xB6] = (Opcode) {"LDX", x, y, 4, op_LD, AM_ZP};
    cpu->opcodes[0xAE] = (Opcode) {"LDX", x, 0, 4, op_LD, AM_ABSOLUTE};
    cpu->opcodes[0xBE] = (Opcode) {"LDX", x, y, -4, op_LD, AM_ABSOLUTE};
    cpu->opcodes[0xA4] = (Opcode) {"LDY", y, 0, 3, op_LD, AM_ZP};
    cpu->opcodes[0xB4] = (Opcode) {"LDY", y, x, 4, op_LD, AM_ZP};
    cpu->opcodes[0xAC] = (Opcode) {"LDY", y, 0, 4, op_LD, AM_ABSOLUTE};
    cpu->opcodes[0xBC] = (Opcode) {"LDY", y, x, -4, op_LD, AM_ABSOLUTE};
    
    cpu->opcodes[0x85] = (Opcode) {"STA", a, 0, 3, op_ST, AM_ZP};
    cpu->opcodes[0x95] = (Opcode) {"STA", a, x, 4, op_ST, AM_ZP};
    cpu->opcodes[0x8D] = (Opcode) {"STA", a, 0, 4, op_ST, AM_ABSOLUTE};
    cpu->opcodes[0x9D] = (Opcode) {"STA", a, x, 5, op_ST, AM_ABSOLUTE};
    cpu->opcodes[0x99] = (Opcode) {"STA", a, y, 5, op_ST, AM_ABSOLUTE};
    cpu->opcodes[0x81] = (Opcode) {"STA", a, 0, 6, op_ST, AM_INDIRECT_X};
    cpu->opcodes[0x91] = (Opcode) {"STA", a, 0, 6, op_ST, AM_INDIRECT_Y};
    cpu->opcodes[0x86] = (Opcode) {"STX", x, 0, 3, op_ST, AM_ZP};
    cpu->opcodes[0x96] = (Opcode) {"STX", x, y, 4, op_ST, AM_ZP};
    cpu->opcodes[0x8E] = (Opcode) {"STX", x, 0, 4, op_ST, AM_ABSOLUTE};
    cpu->opcodes[0x84] = (Opcode) {"STY", y, 0, 3, op_ST, AM_ZP};
    cpu->opcodes[0x94] = (Opcode) {"STY", y, x, 4, op_ST, AM_ZP};
    cpu->opcodes[0x8C] = (Opcode) {"STY", y, 0, 4, op_ST, AM_ABSOLUTE};
    
    cpu->opcodes[0x48] = (Opcode) {"PHA", a, 0, 3, op_PH, AM_IMPLIED};
    cpu->opcodes[0x08] = (Opcode) {"PHP", p, 0, 3, op_PH, AM_IMPLIED};
    cpu->opcodes[0x68] = (Opcode) {"PLA", a, 0, 4, op_PL, AM_IMPLIED};
    cpu->opcodes[0x28] = (Opcode) {"PLP", p, 0, 4, op_PL, AM_IMPLIED};
    
    cpu->opcodes[0x69] = (Opcode) {"ADC", 0, 0, 2, op_ADC, AM_IMMEDIATE};
    cpu->opcodes[0x65] = (Opcode) {"ADC", 0, 0, 3, op_ADC, AM_ZP};
    cpu->opcodes[0x75] = (Opcode) {"ADC", 0, x, 4, op_ADC, AM_ZP};
    cpu->opcodes[0x6D] = (Opcode) {"ADC", 0, 0, 4, op_ADC, AM_ABSOLUTE};
    cpu->opcodes[0x7D] = (Opcode) {"ADC", 0, x, -4, op_ADC, AM_ABSOLUTE};
    cpu->opcodes[0x79] = (Opcode) {"ADC", 0, y, -4, op_ADC, AM_ABSOLUTE};
    cpu->opcodes[0x61] = (Opcode) {"ADC", 0, 0, 6, op_ADC, AM_INDIRECT_X};
    cpu->opcodes[0x71] = (Opcode) {"ADC", 0, 0, -5, op_ADC, AM_INDIRECT_Y};
    
    cpu->opcodes[0xE9] = (Opcode) {"SBC", 0, 0, 2, op_SBC, AM_IMMEDIATE};
    cpu->opcodes[0xE5] = (Opcode) {"SBC", 0, 0, 3, op_SBC, AM_ZP};
    cpu->opcodes[0xF5] = (Opcode) {"SBC", 0, x, 4, op_SBC, AM_ZP};
    cpu->opcodes[0xED] = (Opcode) {"SBC", 0, 0, 4, op_SBC, AM_ABSOLUTE};
    cpu->opcodes[0xFD] = (Opcode) {"SBC", 0, x, -4, op_SBC, AM_ABSOLUTE};
    cpu->opcodes[0xF9] = (Opcode) {"SBC", 0, y, -4, op_SBC, AM_ABSOLUTE};
    cpu->opcodes[0xE1] = (Opcode) {"SBC", 0, 0, 6, op_SBC, AM_INDIRECT_X};
    cpu->opcodes[0xF1] = (Opcode) {"SBC", 0, 0, -5, op_SBC, AM_INDIRECT_Y};
    
    cpu->opcodes[0x29] = (Opcode) {"AND", 0, 0, 2, op_AND, AM_IMMEDIATE};
    cpu->opcodes[0x25] = (Opcode) {"AND", 0, 0, 3, op_AND, AM_ZP};
    cpu->opcodes[0x35] = (Opcode) {"AND", 0, x, 4, op_AND, AM_ZP};
    cpu->opcodes[0x2D] = (Opcode) {"AND", 0, 0, 4, op_AND, AM_ABSOLUTE};
    cpu->opcodes[0x3D] = (Opcode) {"AND", 0, x, -4, op_AND, AM_ABSOLUTE};
    cpu->opcodes[0x39] = (Opcode) {"AND", 0, y, -4, op_AND, AM_ABSOLUTE};
    cpu->opcodes[0x21] = (Opcode) {"AND", 0, 0, 6, op_AND, AM_INDIRECT_X};
    cpu->opcodes[0x31] = (Opcode) {"AND", 0, 0, -5, op_AND, AM_INDIRECT_Y};
    
    cpu->opcodes[0x49] = (Opcode) {"EOR", 0, 0, 2, op_EOR, AM_IMMEDIATE};
    cpu->opcodes[0x45] = (Opcode) {"EOR", 0, 0, 3, op_EOR, AM_ZP};
    cpu->opcodes[0x55] = (Opcode) {"EOR", 0, x, 4, op_EOR, AM_ZP};
    cpu->opcodes[0x4D] = (Opcode) {"EOR", 0, 0, 4, op_EOR, AM_ABSOLUTE};
    cpu->opcodes[0x5D] = (Opcode) {"EOR", 0, x, -4, op_EOR, AM_ABSOLUTE};
    cpu->opcodes[0x59] = (Opcode) {"EOR", 0, y, -4, op_EOR, AM_ABSOLUTE};
    cpu->opcodes[0x41] = (Opcode) {"EOR", 0, 0, 6, op_EOR, AM_INDIRECT_X};
    cpu->opcodes[0x51] = (Opcode) {"EOR", 0, 0, -5, op_EOR, AM_INDIRECT_Y};
    
    cpu->opcodes[0x09] = (Opcode) {"ORA", 0, 0, 2, op_ORA, AM_IMMEDIATE};
    cpu->opcodes[0x05] = (Opcode) {"ORA", 0, 0, 3, op_ORA, AM_ZP};
    cpu->opcodes[0x15] = (Opcode) {"ORA", 0, x, 4, op_ORA, AM_ZP};
    cpu->opcodes[0x0D] = (Opcode) {"ORA", 0, 0, 4, op_ORA, AM_ABSOLUTE};
    cpu->opcodes[0x1D] = (Opcode) {"ORA", 0, x, -4, op_ORA, AM_ABSOLUTE};
    cpu->opcodes[0x19] = (Opcode) {"ORA", 0, y, -4, op_ORA, AM_ABSOLUTE};
    cpu->opcodes[0x01] = (Opcode) {"ORA", 0, 0, 6, op_ORA, AM_INDIRECT_X};
    cpu->opcodes[0x11] = (Opcode) {"ORA", 0, 0, -5, op_ORA, AM_INDIRECT_Y};
    
    cpu->opcodes[0xC9] = (Opcode) {"CMP", a, 0, 2, op_CMP, AM_IMMEDIATE};
    cpu->opcodes[0xC5] = (Opcode) {"CMP", a, 0, 3, op_CMP, AM_ZP};
    cpu->opcodes[0xD5] = (Opcode) {"CMP", a, x, 4, op_CMP, AM_ZP};
    cpu->opcodes[0xCD] = (Opcode) {"CMP", a, 0, 4, op_CMP, AM_ABSOLUTE};
    cpu->opcodes[0xDD] = (Opcode) {"CMP", a, x, -4, op_CMP, AM_ABSOLUTE};
    cpu->opcodes[0xD9] = (Opcode) {"CMP", a, y, -4, op_CMP, AM_ABSOLUTE};
    cpu->opcodes[0xC1] = (Opcode) {"CMP", a, 0, 6, op_CMP, AM_INDIRECT_X};
    cpu->opcodes[0xD1] = (Opcode) {"CMP", a, 0, -5, op_CMP, AM_INDIRECT_Y};
    cpu->opcodes[0xE0] = (Opcode) {"CPX", x, 0, 2, op_CMP, AM_IMMEDIATE};
    cpu->opcodes[0xE4] = (Opcode) {"CPX", x, 0, 3, op_CMP, AM_ZP};
    cpu->opcodes[0xEC] = (Opcode) {"CPX", x, 0, 4, op_CMP, AM_ABSOLUTE};
    cpu->opcodes[0xC0] = (Opcode) {"CPY", y, 0, 2, op_CMP, AM_IMMEDIATE};
    cpu->opcodes[0xC4] = (Opcode) {"CPY", y, 0, 3, op_CMP, AM_ZP};
    cpu->opcodes[0xCC] = (Opcode) {"CPY", y, 0, 4, op_CMP, AM_ABSOLUTE};
    
    cpu->opcodes[0x24] = (Opcode) {"BIT", 0, 0, 3, op_BIT, AM_ZP};
    cpu->opcodes[0x2C] = (Opcode) {"BIT", 0, 0, 4, op_BIT, AM_ABSOLUTE};
    
    cpu->opcodes[0xE6] = (Opcode) {"INC", 0, 0, 5, op_INC, AM_ZP};
    cpu->opcodes[0xF6] = (Opcode) {"INC", 0, x, 6, op_INC, AM_ZP};
    cpu->opcodes[0xEE] = (Opcode) {"INC", 0, 0, 6, op_INC, AM_ABSOLUTE};
    cpu->opcodes[0xFE] = (Opcode) {"INC", 0, x, 7, op_INC, AM_ABSOLUTE};
    cpu->opcodes[0xE8] = (Opcode) {"INX", x, 0, 2, op_IN, AM_IMPLIED};
    cpu->opcodes[0xC8] = (Opcode) {"INY", y, 0, 2, op_IN, AM_IMPLIED};
    
    cpu->opcodes[0xC6] = (Opcode) {"DEC", 0, 0, 5, op_DEC, AM_ZP};
    cpu->opcodes[0xD6] = (Opcode) {"DEC", 0, x, 6, op_DEC, AM_ZP};
    cpu->opcodes[0xCE] = (Opcode) {"DEC", 0, 0, 6, op_DEC, AM_ABSOLUTE};
    cpu->opcodes[0xDE] = (Opcode) {"DEC", 0, x, 7, op_DEC, AM_ABSOLUTE};
    cpu->opcodes[0xCA] = (Opcode) {"DEX", x, 0, 2, op_DE, AM_IMPLIED};
    cpu->opcodes[0x88] = (Opcode) {"DEY", y, 0, 2, op_DE, AM_IMPLIED};
    
    cpu->opcodes[0x0A] = (Opcode) {"ASL A", a, 0, 2, op_ASL, AM_IMPLIED};
    cpu->opcodes[0x06] = (Opcode) {"ASL", 0, 0, 5, op_ASL, AM_ZP};
    cpu->opcodes[0x16] = (Opcode) {"ASL", 0, x, 6, op_ASL, AM_ZP};
    cpu->opcodes[0x0E] = (Opcode) {"ASL", 0, 0, 6, op_ASL, AM_ABSOLUTE};
    cpu->opcodes[0x1E] = (Opcode) {"ASL", 0, x, 7, op_ASL, AM_ABSOLUTE};
    
    cpu->opcodes[0x0A] = (Opcode) {"LSR A", a, 0, 2, op_LSR, AM_IMPLIED};
    cpu->opcodes[0x06] = (Opcode) {"LSR", 0, 0, 5, op_LSR, AM_ZP};
    cpu->opcodes[0x16] = (Opcode) {"LSR", 0, x, 6, op_LSR, AM_ZP};
    cpu->opcodes[0x0E] = (Opcode) {"LSR", 0, 0, 6, op_LSR, AM_ABSOLUTE};
    cpu->opcodes[0x1E] = (Opcode) {"LSR", 0, x, 7, op_LSR, AM_ABSOLUTE};
    
    cpu->opcodes[0x0A] = (Opcode) {"ROL A", a, 0, 2, op_ROL, AM_IMPLIED};
    cpu->opcodes[0x06] = (Opcode) {"ROL", 0, 0, 5, op_ROL, AM_ZP};
    cpu->opcodes[0x16] = (Opcode) {"ROL", 0, x, 6, op_ROL, AM_ZP};
    cpu->opcodes[0x0E] = (Opcode) {"ROL", 0, 0, 6, op_ROL, AM_ABSOLUTE};
    cpu->opcodes[0x1E] = (Opcode) {"ROL", 0, x, 7, op_ROL, AM_ABSOLUTE};
    
    cpu->opcodes[0x0A] = (Opcode) {"ROR A", a, 0, 2, op_ROR, AM_IMPLIED};
    cpu->opcodes[0x06] = (Opcode) {"ROR", 0, 0, 5, op_ROR, AM_ZP};
    cpu->opcodes[0x16] = (Opcode) {"ROR", 0, x, 6, op_ROR, AM_ZP};
    cpu->opcodes[0x0E] = (Opcode) {"ROR", 0, 0, 6, op_ROR, AM_ABSOLUTE};
    cpu->opcodes[0x1E] = (Opcode) {"ROR", 0, x, 7, op_ROR, AM_ABSOLUTE};
    
    cpu->opcodes[0x4C] = (Opcode) {"JMP", 0, 0, 3, op_JMP, AM_ABSOLUTE};
    cpu->opcodes[0x6C] = (Opcode) {"JMP", 0, 0, 5, op_JMP, AM_INDIRECT_WORD};
    cpu->opcodes[0x20] = (Opcode) {"JSR", 0, 0, 6, op_JSR, AM_ABSOLUTE};
    cpu->opcodes[0x40] = (Opcode) {"RTI", 0, 0, 6, op_RTI, AM_IMPLIED};
    cpu->opcodes[0x60] = (Opcode) {"RTS", 0, 0, 6, op_RTS, AM_IMPLIED};
    
    cpu->opcodes[0x10] = (Opcode) {"BPL", 0, 0, 2, op_BPL, AM_RELATIVE};
    cpu->opcodes[0x30] = (Opcode) {"BMI", 0, 0, 2, op_BMI, AM_RELATIVE};
    cpu->opcodes[0x50] = (Opcode) {"BVC", 0, 0, 2, op_BVC, AM_RELATIVE};
    cpu->opcodes[0x70] = (Opcode) {"BVS", 0, 0, 2, op_BVS, AM_RELATIVE};
    cpu->opcodes[0x90] = (Opcode) {"BCC", 0, 0, 2, op_BCC, AM_RELATIVE};
    cpu->opcodes[0xB0] = (Opcode) {"BCS", 0, 0, 2, op_BCS, AM_RELATIVE};
    cpu->opcodes[0xD0] = (Opcode) {"BNE", 0, 0, 2, op_BNE, AM_RELATIVE};
    cpu->opcodes[0xF0] = (Opcode) {"BEQ", 0, 0, 2, op_BEQ, AM_RELATIVE};
    
    cpu->opcodes[0x00] = (Opcode) {"BRK", 0, 0, 0, op_BRK, AM_IMPLIED};
    
    cpu->opcodes[0x18] = (Opcode) {"CLC", 0, 0, 2, op_CLC, AM_IMPLIED};
    cpu->opcodes[0x58] = (Opcode) {"CLI", 0, 0, 2, op_CLI, AM_IMPLIED};
    cpu->opcodes[0xD8] = (Opcode) {"CLD", 0, 0, 2, op_CLD, AM_IMPLIED};
    cpu->opcodes[0xB8] = (Opcode) {"CLV", 0, 0, 2, op_CLV, AM_IMPLIED};
    cpu->opcodes[0x38] = (Opcode) {"SEC", 0, 0, 2, op_SEC, AM_IMPLIED};
    cpu->opcodes[0x78] = (Opcode) {"SEI", 0, 0, 2, op_SEI, AM_IMPLIED};
    cpu->opcodes[0xF8] = (Opcode) {"SED", 0, 0, 2, op_SED, AM_IMPLIED};

    cpu->opcodes[0xEA] = (Opcode) {"NOP", 0, 0, 2, NULL, AM_IMPLIED};
}

int cpu_step(CPUState *cpu) {
    // Fetch next instruction
    uint8_t inst = mm_read(cpu->mm, cpu->pc++);
    const Opcode *op = &cpu->opcodes[inst];
    if (!op->name) {
        printf("Invalid Opcode %d\n", inst);
        return -1;
    }
    
    // Fetch parameter, if any
    uint8_t zp_addr;
    uint16_t pre_indexing = 0;
    OpParam param;
    switch (op->am) {
        case AM_IMPLIED:
            param.addr = 0;
            break;
        case AM_IMMEDIATE:
            param.immediate_value[0] = mm_read(cpu->mm, cpu->pc++);
            break;
        case AM_ZP:
            zp_addr = mm_read(cpu->mm, cpu->pc++);
            if (op->reg2) {
                zp_addr += *op->reg2;
            }
            param.addr = zp_addr;
            break;
        case AM_ABSOLUTE:
            pre_indexing = param.addr = mm_read_word(cpu->mm, cpu->pc);
            cpu->pc += 2;
            if (op->reg2) {
                param.addr += *op->reg2;
            }
            break;
        case AM_INDIRECT_WORD:
            param.addr = mm_read_word(cpu->mm, mm_read_word(cpu->mm, cpu->pc));
            cpu->pc += 2;
            break;
        case AM_INDIRECT_X:
            zp_addr = mm_read(cpu->mm, cpu->pc++) + cpu->x;
            param.addr = mm_read_word(cpu->mm, zp_addr);
            break;
        case AM_INDIRECT_Y:
            pre_indexing = mm_read_word(cpu->mm, mm_read(cpu->mm, cpu->pc++));
            param.addr = pre_indexing + cpu->y;
            break;
        case AM_RELATIVE:
            param.relative_addr[0] = mm_read(cpu->mm, cpu->pc++);
            break;
    }
    
    if (op->cycles < 0) {
        cpu->t = abs(op->cycles);
        apply_page_boundary_penalty(cpu, pre_indexing, param.addr);
    } else {
        cpu->t = op->cycles;
    }
    
    if (cpu->verbose) {
        cpu_debug_print_state(cpu);
        printf(" %s", op->name);
        switch (op->am) {
            case AM_IMPLIED:
                break;
            case AM_IMMEDIATE:
                printf(" #$%02x", param.immediate_value[0]);
                break;
            case AM_ZP:
                printf(" $%02x", param.addr);
                break;
            case AM_ABSOLUTE:
                printf(" $%04x", param.addr);
                break;
            case AM_INDIRECT_WORD:
                printf(" ($%04x)", param.addr);
                break;
            case AM_INDIRECT_X:
                printf(" ($%02x,X)", param.addr);
                break;
            case AM_INDIRECT_Y:
                printf(" ($%02x),Y", param.addr);
                break;
            case AM_RELATIVE:
                printf(" %+d", param.relative_addr[0]);
                break;
        }
        if (op->am == AM_ZP || op->am == AM_ABSOLUTE) {
            if (op->reg2 == &cpu->x) {
                printf(",X");
            } else if (op->reg2 == &cpu->y) {
                printf(",Y");
            }
        }
        printf("\n");
    }
    
    if (op->func) {
        (*op->func)(cpu, op, param);
    }
    
    return cpu->t;
}

int cpu_irq(CPUState *cpu) {
    if (get_p_flag(cpu, P_I)) {
        return 0;
    }
    return interrupt(cpu, false, IVT_IRQ);
}

int cpu_nmi(CPUState *cpu) {
    return interrupt(cpu, false, IVT_NMI);
}

int cpu_reset(CPUState *cpu) {
    return interrupt(cpu, true, IVT_RESET);
}

