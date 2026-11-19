/* Synthetic instruction metadata for the JMP translator. Use the real frontend
   for segment checks, loop context restoration and deferred cycle accounting. */
#include "../../src/codegen/codegen.c"

static void begin_timing_block(void)
{
    /* The fixture emits its own per-operation cycle deductions. */
}

void microbench_init_frontend(void)
{
    codegen_instructions[0].pc = 0x100;
    codegen_instructions[0].op_ea_seg = &cpu_state.seg_ds;
    codegen_instructions[0].op_32 = 0x100;
    codegen_instructions[0].flags_op = FLAGS_UNKNOWN;
    codegen_timing_block_start = begin_timing_block;
}

/* The backend emits branches to these CPU and tracked-page helpers. Every
   case uses normal synthetic RAM and valid instructions. Detect a bad branch
   during validation instead of silently running a dummy implementation. */
cpu_events_t cpu_events;
int codegen_flat_es, codegen_in_recompile, mem_a20_reset_line, mem_a20_reset_line_fetch;
uint8_t high_page, page_ff[4096], znptable8[256];
uint32_t mem_logical_addr;
page_t *page_lookup[1048576];

void codegen_sse_unavailable(void) { fatal("unexpected SSE exception"); }
void fpu_postamble(void) { fatal("unexpected x87 instruction"); }
void dynarec_rf_after_op(void) { fatal("unexpected debug instruction"); }
void dynarec_lmsw(uint16_t value) { fatal("unexpected LMSW %u", value); }
int dynarec_mov_cr0(uint32_t r) { fatal("unexpected MOV CR0 %u", r); return 0; }
void dynarec_mov_cr2(uint32_t r) { fatal("unexpected MOV CR2 %u", r); }
int dynarec_mov_cr3(uint32_t r) { fatal("unexpected MOV CR3 %u", r); return 0; }
int dynarec_mov_cr4(uint32_t r) { fatal("unexpected MOV CR4 %u", r); return 0; }
int dynarec_mov_drx(uint32_t op) { fatal("unexpected MOV DR %u", op); return 0; }
int dynarec_fetch_walk(uint32_t addr) { fatal("unexpected fetch walk %x", addr); return 0; }

void page_add_to_evict_list(page_t *page) { fatal("unexpected page eviction"); }
void mem_write_ramb_page(uint32_t addr, uint8_t val, page_t *page) { fatal("unexpected tracked byte write"); }
void mem_write_ramw_page(uint32_t addr, uint16_t val, page_t *page) { fatal("unexpected tracked word write"); }
void mem_write_raml_page(uint32_t addr, uint32_t val, page_t *page) { fatal("unexpected tracked dword write"); }
