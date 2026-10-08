/* Use a small real allocator arena to force eviction and chunk reuse. */
#include <setjmp.h>
#include <stdarg.h>
#include <string.h>

#define MEM_BLOCK_NR 8
#include "../../src/codegen_new/codegen_allocator.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "line %d: %s failed\n", __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

static codeblock_t blocks[32];
codeblock_t *codeblock = blocks;
static uint8_t arena[MEM_BLOCK_NR * MEM_BLOCK_SIZE];
static unsigned evictions, last_evicted;
static int expect_full;
static jmp_buf full;

void *plat_mmap(size_t size, uint8_t executable, uint8_t *large)
{
    CHECK(size == sizeof(arena) && executable);
    *large = 0;
    return arena;
}

void pclog(const char *fmt, ...) { (void) fmt; }

void fatal(const char *fmt, ...)
{
    if (expect_full)
        longjmp(full, 1);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

void codegen_delete_block(codeblock_t *block)
{
    CHECK(block->head_mem_block != NULL);
    last_evicted = block - blocks;
    evictions++;
    codegen_allocator_free(block->head_mem_block);
    block->head_mem_block = NULL;
    block->data = NULL;
}

static void allocate(unsigned nr)
{
    mem_block_t *chunk = codegen_allocator_allocate(blocks[nr].head_mem_block, nr);
    if (!blocks[nr].head_mem_block) {
        blocks[nr].head_mem_block = chunk;
        blocks[nr].data = codeblock_allocator_get_ptr(chunk);
    }
    memset(codeblock_allocator_get_ptr(chunk), nr, MEM_BLOCK_SIZE);
}

static void clear_blocks(void)
{
    for (unsigned nr = 0; nr < 32; nr++)
        if (blocks[nr].head_mem_block)
            codegen_delete_block(&blocks[nr]);
    CHECK(codegen_allocator_usage == 0);
    CHECK(mem_code_block_head == NULL && mem_code_block_tail == NULL);
    evictions = 0;
}

static void check_survivors(void)
{
    unsigned chunks = 0, listed = 0;
    mem_code_block_t *previous = NULL;
    for (mem_code_block_t *node = mem_code_block_head; node; node = node->next) {
        CHECK(node->prev == previous);
        CHECK(blocks[node->number].head_mem_block != NULL);
        previous = node;
        CHECK(++listed <= 32);
    }
    CHECK(previous == mem_code_block_tail);
    for (unsigned nr = 0; nr < 32; nr++) {
        CHECK(valid_code_blocks[nr] == (blocks[nr].head_mem_block != NULL));
        for (mem_block_t *chunk = blocks[nr].head_mem_block; chunk;
             chunk = chunk->next ? &mem_blocks[chunk->next - 1] : NULL) {
            uint8_t *data = codeblock_allocator_get_ptr(chunk);
            for (unsigned i = 0; i < MEM_BLOCK_SIZE; i++)
                CHECK(data[i] == nr);
            CHECK(++chunks <= MEM_BLOCK_NR);
        }
    }
    CHECK(chunks == (unsigned) codegen_allocator_usage);
}

int main(void)
{
    codegen_allocator_init();
    /* Extending the oldest, a middle, or the newest block must protect it. */
    for (unsigned active = 1; active <= MEM_BLOCK_NR; active++) {
        for (unsigned nr = 1; nr <= MEM_BLOCK_NR; nr++)
            allocate(nr);
        allocate(active);
        CHECK(evictions == 1 && last_evicted == (active == 1 ? 2 : 1));
        CHECK(codegen_allocator_usage == MEM_BLOCK_NR);
        check_survivors();
        clear_blocks();
    }

    /* A multi-chunk victim leaves spare chunks for the next allocations. */
    allocate(1);
    allocate(1);
    allocate(1);
    for (unsigned nr = 2; nr <= 6; nr++)
        allocate(nr);
    allocate(7);
    CHECK(evictions == 1 && last_evicted == 1);
    CHECK(codegen_allocator_usage == 6);
    allocate(8);
    allocate(9);
    CHECK(evictions == 1 && codegen_allocator_usage == MEM_BLOCK_NR);
    check_survivors();
    clear_blocks();

    /* A sole owner can be evicted for a new block, but never for itself. */
    for (unsigned i = 0; i < MEM_BLOCK_NR; i++)
        allocate(1);
    expect_full = 1;
    if (!setjmp(full)) {
        allocate(1);
        CHECK(0);
    }
    expect_full = 0;
    CHECK(evictions == 0 && codegen_allocator_usage == MEM_BLOCK_NR);
    allocate(2);
    CHECK(evictions == 1 && last_evicted == 1 && codegen_allocator_usage == 1);
    check_survivors();
    clear_blocks();

    /* Repeated wraparound exercises both list ends and recycled block IDs. */
    for (unsigned i = 0; i < 1000; i++) {
        allocate(1 + i % 16);
        CHECK(evictions == (i < MEM_BLOCK_NR ? 0 : i + 1 - MEM_BLOCK_NR));
        check_survivors();
    }
    clear_blocks();
    puts("codegen allocator: eviction and chunk reuse passed");
    return 0;
}
