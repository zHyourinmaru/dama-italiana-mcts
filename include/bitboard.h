/*
 * bitboard.h - Bitboard utilities, precomputed tables, square mapping
 *
 * Square numbering (Italian board, dark square at bottom-left for White):
 *
 *   Row 7 (Black home):  28  29  30  31
 *   Row 6:               24  25  26  27
 *   Row 5:               20  21  22  23
 *   Row 4:               16  17  18  19
 *   Row 3:               12  13  14  15
 *   Row 2:                8   9  10  11
 *   Row 1:                4   5   6   7
 *   Row 0 (White home):   0   1   2   3
 *
 * Even rows: dark squares in columns 1,3,5,7
 * Odd  rows: dark squares in columns 0,2,4,6
 */

#ifndef BITBOARD_H
#define BITBOARD_H

#include "types.h"

/* ------------------------------------------------------------------ */
/*  Inline bit utilities                                               */
/* ------------------------------------------------------------------ */

static inline int popcount32(Bitboard b) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_popcount(b);
#elif defined(_MSC_VER)
    return (int)__popcnt(b);
#else
    b = b - ((b >> 1) & 0x55555555u);
    b = (b & 0x33333333u) + ((b >> 2) & 0x33333333u);
    return (int)(((b + (b >> 4)) & 0x0F0F0F0Fu) * 0x01010101u >> 24);
#endif
}

static inline int lsb_index(Bitboard b) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_ctz(b);
#elif defined(_MSC_VER)
    unsigned long idx;
    _BitScanForward(&idx, b);
    return (int)idx;
#else
    /* de Bruijn fallback */
    static const int DeBruijnTable[32] = {
        0,1,28,2,29,14,24,3,30,22,20,15,25,17,4,8,
        31,27,13,23,21,19,16,7,26,12,18,6,11,5,10,9
    };
    return DeBruijnTable[((b & (-(int32_t)b)) * 0x077CB531u) >> 27];
#endif
}

/* Iterate over set bits */
#define FOR_EACH_BIT(bb, sq) \
    for (Bitboard _tmp = (bb); _tmp && ((sq) = lsb_index(_tmp), 1); _tmp &= _tmp - 1)

/* ------------------------------------------------------------------ */
/*  Square ↔ board-coordinate conversion                              */
/* ------------------------------------------------------------------ */

static inline int sq_to_row(int sq) { return sq / 4; }
static inline int sq_to_col(int sq) {
    int r = sq / 4;
    int c = (sq % 4) * 2;
    return (r % 2 == 0) ? c + 1 : c;
}

/* Convert board col,row → square index (-1 if not a dark square) */
static inline int rowcol_to_sq(int row, int col) {
    if ((row + col) % 2 == 0) return -1;  /* light square */
    return row * 4 + col / 2;
}

/* ------------------------------------------------------------------ */
/*  Precomputed lookup tables                                         */
/* ------------------------------------------------------------------ */

/* Move masks: quiet single-step diagonal destinations                 */
extern Bitboard man_move_mask[2][NUM_SQUARES];   /* [color][sq]        */
extern Bitboard king_move_mask[NUM_SQUARES];      /* [sq]               */

/* Jump masks: landing squares for single captures                     */
extern Bitboard man_jump_mask[2][NUM_SQUARES];   /* [color][sq]        */
extern Bitboard king_jump_mask[NUM_SQUARES];      /* [sq]               */

/* Mid-square lookup: given (from, to) of a jump, the captured square  */
extern int8_t   jump_mid[NUM_SQUARES][NUM_SQUARES]; /* -1 if invalid   */

/* 4 diagonal neighbour tables for each square                         */
#define DIR_NE  0   /* north-east (+row, +col) */
#define DIR_NW  1   /* north-west (+row, -col) */
#define DIR_SE  2   /* south-east (-row, +col) */
#define DIR_SW  3   /* south-west (-row, -col) */
#define NUM_DIRS 4

extern int8_t   diag_neighbour[NUM_SQUARES][NUM_DIRS]; /* -1 if off board */
extern int8_t   diag_jump[NUM_SQUARES][NUM_DIRS];      /* landing sq, -1  */

/* Row masks */
extern Bitboard row_mask[BOARD_SIZE];
#define PROMOTION_ROW_WHITE  row_mask[7]   /* row 7 = Black's home row  */
#define PROMOTION_ROW_BLACK  row_mask[0]   /* row 0 = White's home row  */

/* Zobrist keys */
extern uint64_t zobrist_piece[2][NUM_SQUARES]; /* [color][sq]           */
extern uint64_t zobrist_king[NUM_SQUARES];     /* extra XOR for king    */
extern uint64_t zobrist_turn;                   /* XOR when black moves  */

/* ------------------------------------------------------------------ */
/*  Initialization                                                     */
/* ------------------------------------------------------------------ */

void bitboard_init(void);

/* ------------------------------------------------------------------ */
/*  Starting position                                                  */
/* ------------------------------------------------------------------ */

GameState game_initial_state(void);

#endif /* BITBOARD_H */
