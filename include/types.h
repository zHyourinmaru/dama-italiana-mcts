/*
 * types.h - Common types and constants for Dama Italiana
 *
 * Bitboard-based Italian Checkers engine.
 * Each bit in a uint32_t maps to one of the 32 dark squares.
 */

#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Board geometry                                                     */
/* ------------------------------------------------------------------ */

#define BOARD_SIZE      8
#define NUM_SQUARES     32      /* only dark squares are used           */
#define MAX_MOVES       48      /* generous upper bound per position    */
#define MAX_CAPTURES    12      /* max pieces captured in one sequence  */
#define MAX_PLY         400     /* safety cap for game length           */

/* ------------------------------------------------------------------ */
/*  Fundamental types                                                  */
/* ------------------------------------------------------------------ */

typedef uint32_t Bitboard;      /* 32-bit mask over the dark squares   */

typedef enum { WHITE = 0, BLACK = 1 } Color;

#define OPPONENT(c) ((Color)(1 - (c)))

/* ------------------------------------------------------------------ */
/*  Game state  – fully described by three bitboards + metadata        */
/* ------------------------------------------------------------------ */

typedef struct {
    Bitboard wp;            /* white pieces (men + kings)               */
    Bitboard bp;            /* black pieces (men + kings)               */
    Bitboard k;             /* kings of either colour                   */
    Color    turn;          /* side to move                             */
    int      ply;           /* half-move clock (total moves played)     */
    int      no_progress;   /* half-moves since last capture/promotion  */
    uint64_t hash;          /* Zobrist hash for repetition detection    */
} GameState;

/* ------------------------------------------------------------------ */
/*  Move representation                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t  from;                      /* origin square  (0..31)      */
    uint8_t  to;                        /* destination     (0..31)     */
    uint8_t  captures[MAX_CAPTURES];    /* squares of captured pieces  */
    uint8_t  n_captures;                /* number of pieces captured   */
    bool     promotion;                 /* does this move promote?     */
    /* -- Italian priority metadata (used during generation) -- */
    bool     is_king_move;              /* is the moving piece a king? */
    uint8_t  king_captures;             /* # of opponent kings captured*/
    uint8_t  first_king_idx;            /* index of first king in seq  */
} Move;

/* ------------------------------------------------------------------ */
/*  Game result                                                        */
/* ------------------------------------------------------------------ */

typedef enum {
    RESULT_ONGOING =  2,
    RESULT_WHITE   =  1,
    RESULT_DRAW    =  0,
    RESULT_BLACK   = -1
} GameResult;

/* ------------------------------------------------------------------ */
/*  MCTS policy selection                                              */
/* ------------------------------------------------------------------ */

typedef enum {
    POLICY_UCB1 = 0,
    POLICY_PUCT = 1
} PolicyType;

/* ------------------------------------------------------------------ */
/*  Time budgets                                                       */
/* ------------------------------------------------------------------ */

static const double TIME_BUDGETS[] = { 0.2, 1.0, 3.0 };
#define NUM_TIME_BUDGETS 3

#endif /* TYPES_H */
