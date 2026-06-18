/*
 * rng.h - Fast XorShift64 RNG, shared between bitboard and MCTS modules
 *
 * Both modules previously maintained identical copies of these functions.
 * Centralising them here avoids divergence and keeps the TU-local semantics
 * (static inline → no linkage issues).
 */

#ifndef RNG_H
#define RNG_H

#include <stdint.h>

/* XorShift64 — period 2^64-1, passes BigCrush */
static inline uint64_t xorshift64(uint64_t *s) {
    uint64_t x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

/* Uniform random integer in [0, n) */
static inline int rand_int(uint64_t *rng, int n) {
    return (int)(xorshift64(rng) % (uint64_t)n);
}

#endif /* RNG_H */
