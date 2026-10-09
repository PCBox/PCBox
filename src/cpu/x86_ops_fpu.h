/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
/* Run an x87 instruction and record it as the last one for FSTENV/FSAVE.
   Under SoftFloat, an instruction that did not complete (a fault, #NM, or a
   pending exception that stopped it) leaves the last-instruction pointers
   alone, and an unmasked exception it raised is signalled once it is done. */
static int
opESCAPE(OpFn op, uint32_t fetchdat)
{
    int ret;
    int raised = 0;

    x87_op         = ((opcode & 0x07) << 8) | (fetchdat & 0xff);
    fpu_sf_new_exc = 0;
    ret            = op(fetchdat);
    if (fpu_softfloat) {
        raised = fpu_sf_report_exception();
        if (ret)
            return ret;
    }
    cpu_state.fpu_op = x87_op;
    cpu_state.fpu_CS = cpu_state.temp_CS;
    cpu_state.fpu_cs = cpu_state.temp_cs;
    cpu_state.fpu_pc = cpu_state.temp_pc;
    if ((x87_op & 0xff) < 0xc0)
        fpu_postamble();
    return ret || raised;
}

static int
opESCAPE_d8_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_d8_a16[(fetchdat >> 3) & 0x1f], fetchdat);
}
static int
opESCAPE_d8_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_d8_a32[(fetchdat >> 3) & 0x1f], fetchdat);
}

static int
opESCAPE_d9_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_d9_a16[fetchdat & 0xff], fetchdat);
}
static int
opESCAPE_d9_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_d9_a32[fetchdat & 0xff], fetchdat);
}

static int
opESCAPE_da_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_da_a16[fetchdat & 0xff], fetchdat);
}
static int
opESCAPE_da_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_da_a32[fetchdat & 0xff], fetchdat);
}

static int
opESCAPE_db_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_db_a16[fetchdat & 0xff], fetchdat);
}
static int
opESCAPE_db_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_db_a32[fetchdat & 0xff], fetchdat);
}

static int
opESCAPE_dc_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_dc_a16[(fetchdat >> 3) & 0x1f], fetchdat);
}
static int
opESCAPE_dc_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_dc_a32[(fetchdat >> 3) & 0x1f], fetchdat);
}

static int
opESCAPE_dd_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_dd_a16[fetchdat & 0xff], fetchdat);
}
static int
opESCAPE_dd_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_dd_a32[fetchdat & 0xff], fetchdat);
}

static int
opESCAPE_de_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_de_a16[fetchdat & 0xff], fetchdat);
}
static int
opESCAPE_de_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_de_a32[fetchdat & 0xff], fetchdat);
}

static int
opESCAPE_df_a16(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_df_a16[fetchdat & 0xff], fetchdat);
}
static int
opESCAPE_df_a32(uint32_t fetchdat)
{
    return opESCAPE(x86_opcodes_df_a32[fetchdat & 0xff], fetchdat);
}

static int
opWAIT(UNUSED(uint32_t fetchdat))
{
    if ((cr0 & 0xa) == 0xa) {
        x86_int(7);
        return 1;
    }

    if (fpu_softfloat) {
        FPU_check_pending_exceptions();
    }
    CLOCK_CYCLES(4);
    return 0;
}
