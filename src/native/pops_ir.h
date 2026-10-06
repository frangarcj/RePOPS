#ifndef REPOPS_POPS_IR_H
#define REPOPS_POPS_IR_H

/* Recovered meanings, not original Sony identifiers. Values are the original
 * 16-bit category field at record+4; enum size does not define the wire layout.
 */
typedef enum rp_pops_category {
    RP_CAT_EMPTY             = 0x00,
    RP_CAT_JUMP_REGISTER     = 0x01,
    RP_CAT_READ_HILO         = 0x02,
    RP_CAT_MULT_DIV          = 0x03,
    RP_CAT_STORE_COP_MEMORY  = 0x04,
    RP_CAT_EXIT              = 0x05,
    RP_CAT_READ_COP          = 0x06,
    RP_CAT_COP0_CONTROL      = 0x07,
    RP_CAT_LOAD_COP_MEMORY   = 0x08,
    RP_CAT_IMMEDIATE         = 0x09,
    RP_CAT_WRITE_COP         = 0x0A,
    RP_CAT_WRITE_HILO        = 0x0B,
    RP_CAT_BRANCH            = 0x0C,
    RP_CAT_ALU               = 0x0D,
    RP_CAT_JUMP_DIRECT       = 0x0E,
    RP_CAT_EXCEPTION         = 0x0F,
    RP_CAT_MEMORY            = 0x10,
    RP_CAT_SHIFT_IMMEDIATE   = 0x11,
    RP_CAT_GTE               = 0x12,
    RP_CAT_ELIDED            = 0x13
} rp_pops_category;

/* Normalized byte at record+3. Primary opcodes retain their MIPS values;
 * SPECIAL functions are function|0x40. Raw branches are normalized again.
 * These are NOT 32-bit Allegrex output words or instruction encodings.
 */
typedef enum rp_pops_opcode {
    RP_OP_SPECIAL = 0x00, RP_OP_REGIMM = 0x01,
    RP_OP_J = 0x02, RP_OP_JAL = 0x03,
    RP_OP_RAW_BEQ = 0x04, RP_OP_RAW_BNE = 0x05,
    RP_OP_RAW_BLEZ = 0x06, RP_OP_RAW_BGTZ = 0x07,
    RP_OP_ADDI = 0x08, RP_OP_ADDIU = 0x09,
    RP_OP_SLTI = 0x0A, RP_OP_SLTIU = 0x0B,
    RP_OP_ANDI = 0x0C, RP_OP_ORI = 0x0D, RP_OP_XORI = 0x0E,
    RP_OP_LUI = 0x0F, RP_OP_COP0 = 0x10, RP_OP_COP2 = 0x12,
    RP_OP_LB = 0x20, RP_OP_LH = 0x21, RP_OP_LWL = 0x22,
    RP_OP_LW = 0x23, RP_OP_LBU = 0x24, RP_OP_LHU = 0x25, RP_OP_LWR = 0x26,
    RP_OP_SB = 0x28, RP_OP_SH = 0x29, RP_OP_SWL = 0x2A,
    RP_OP_SW = 0x2B, RP_OP_SWR = 0x2E,
    RP_OP_LWC0 = 0x30, RP_OP_LWC2 = 0x32,
    RP_OP_SWC0 = 0x38, RP_OP_SWC1 = 0x39, RP_OP_SWC2 = 0x3A,
    RP_OP_SLL = 0x40, RP_OP_SRL = 0x42, RP_OP_SRA = 0x43,
    RP_OP_SLLV = 0x44, RP_OP_SRLV = 0x46, RP_OP_SRAV = 0x47,
    RP_OP_JR = 0x48, RP_OP_JALR = 0x49,
    RP_OP_SYSCALL = 0x4C, RP_OP_BREAK = 0x4D,
    RP_OP_MFHI = 0x50, RP_OP_MTHI = 0x51,
    RP_OP_MFLO = 0x52, RP_OP_MTLO = 0x53,
    RP_OP_MULT = 0x58, RP_OP_MULTU = 0x59, RP_OP_DIV = 0x5A, RP_OP_DIVU = 0x5B,
    RP_OP_ADD = 0x60, RP_OP_ADDU = 0x61, RP_OP_SUB = 0x62, RP_OP_SUBU = 0x63,
    RP_OP_AND = 0x64, RP_OP_OR = 0x65, RP_OP_XOR = 0x66, RP_OP_NOR = 0x67,
    RP_OP_SLT = 0x6A, RP_OP_SLTU = 0x6B,
    RP_OP_BGEZ = 0xC0, RP_OP_BLTZ = 0xC1,
    RP_OP_BEQ = 0xC2, RP_OP_BNE = 0xC3, RP_OP_BLEZ = 0xC4, RP_OP_BGTZ = 0xC5
} rp_pops_opcode;

/* Helper-only selectors: not decoded guest opcodes. */
typedef enum rp_pops_emit_pseudo {
    RP_EMIT_CONSTANT = 0x80,
    RP_EMIT_SIGN_BYTE = 0x81,
    RP_EMIT_SIGN_HALF = 0x82
} rp_pops_emit_pseudo;

/* +0x46A0 policy for ordinary state destinations, after special COP cases. */
typedef enum rp_pops_state_store_policy {
    RP_STATE_STORE_WORD = 0,
    RP_STATE_STORE_SIGNED_HALF_WORD = 1,
    RP_STATE_STORE_HALF = 2,
    RP_STATE_STORE_IGNORE = 3
} rp_pops_state_store_policy;

static inline const char *rp_pops_category_name(unsigned category)
{
    switch (category) {
    case RP_CAT_EMPTY: return "empty";
    case RP_CAT_JUMP_REGISTER: return "jump_register";
    case RP_CAT_READ_HILO: return "read_hilo";
    case RP_CAT_MULT_DIV: return "mult_div";
    case RP_CAT_STORE_COP_MEMORY: return "store_cop_memory";
    case RP_CAT_EXIT: return "exit";
    case RP_CAT_READ_COP: return "read_cop";
    case RP_CAT_COP0_CONTROL: return "cop0_control";
    case RP_CAT_LOAD_COP_MEMORY: return "load_cop_memory";
    case RP_CAT_IMMEDIATE: return "immediate";
    case RP_CAT_WRITE_COP: return "write_cop";
    case RP_CAT_WRITE_HILO: return "write_hilo";
    case RP_CAT_BRANCH: return "branch";
    case RP_CAT_ALU: return "alu";
    case RP_CAT_JUMP_DIRECT: return "jump_direct";
    case RP_CAT_EXCEPTION: return "exception";
    case RP_CAT_MEMORY: return "memory";
    case RP_CAT_SHIFT_IMMEDIATE: return "shift_immediate";
    case RP_CAT_GTE: return "gte";
    case RP_CAT_ELIDED: return "elided";
    default: return "unknown";
    }
}
#endif
