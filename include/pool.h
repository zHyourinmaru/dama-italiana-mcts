/*
 * pool.h - Fixed-block memory pool for MCTS tree nodes
 *
 * Pre-allocates a large arena and hands out fixed-size blocks in O(1).
 * A single pool_reset() reclaims all memory without per-node free().
 */

#ifndef POOL_H
#define POOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *arena;         /* base pointer of the allocated arena     */
    size_t   block_size;    /* size of each block (aligned)            */
    size_t   capacity;      /* total number of blocks                  */
    size_t   used;          /* number of blocks currently allocated    */
    size_t   arena_bytes;   /* total bytes in the arena                */
} Pool;

/* Initialise pool: allocate arena for `capacity` blocks of `block_size` */
bool pool_init(Pool *p, size_t block_size, size_t capacity);

/* Allocate a single block; returns NULL if exhausted */
void *pool_alloc(Pool *p);

/* Allocate `count` contiguous blocks (for children arrays) */
void *pool_alloc_array(Pool *p, size_t count);

/* Reset pool: all memory reclaimed, no free() calls */
void pool_reset(Pool *p);

/* Destroy pool: free the arena */
void pool_destroy(Pool *p);

/* Statistics */
size_t pool_used(const Pool *p);
size_t pool_capacity(const Pool *p);
double pool_usage_pct(const Pool *p);

#endif /* POOL_H */
