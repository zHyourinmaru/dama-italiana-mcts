/*
 * pool.c - Fixed-block memory pool implementation
 */

#include "pool.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Align block size to 8 bytes for performance */
#define ALIGN_UP(x, a) (((x) + (a) - 1) & ~((a) - 1))

bool pool_init(Pool *p, size_t block_size, size_t capacity) {
    p->block_size = ALIGN_UP(block_size, 8);
    p->capacity   = capacity;
    p->used       = 0;
    p->arena_bytes = p->block_size * capacity;

    p->arena = (uint8_t *)malloc(p->arena_bytes);
    if (!p->arena) {
        fprintf(stderr, "[Pool] Failed to allocate %zu bytes\n", p->arena_bytes);
        return false;
    }
    return true;
}

void *pool_alloc(Pool *p) {
    if (p->used >= p->capacity) return NULL;
    void *ptr = p->arena + p->used * p->block_size;
    p->used++;
    memset(ptr, 0, p->block_size);
    return ptr;
}

void *pool_alloc_array(Pool *p, size_t count) {
    if (p->used + count > p->capacity) return NULL;
    void *ptr = p->arena + p->used * p->block_size;
    p->used += count;
    memset(ptr, 0, count * p->block_size);
    return ptr;
}

void pool_reset(Pool *p) {
    p->used = 0;
}

void pool_destroy(Pool *p) {
    if (p->arena) {
        free(p->arena);
        p->arena = NULL;
    }
    p->capacity = 0;
    p->used = 0;
}

size_t pool_used(const Pool *p)    { return p->used; }
size_t pool_capacity(const Pool *p){ return p->capacity; }

double pool_usage_pct(const Pool *p) {
    if (p->capacity == 0) return 0.0;
    return 100.0 * (double)p->used / (double)p->capacity;
}
