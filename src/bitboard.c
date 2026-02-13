/*
 * bitboard.c - Precomputed tables, Zobrist keys, starting position
 */

#include "bitboard.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/*  Table definitions                                                  */
/* ------------------------------------------------------------------ */

Bitboard man_move_mask[2][NUM_SQUARES];
Bitboard king_move_mask[NUM_SQUARES];
Bitboard man_jump_mask[2][NUM_SQUARES];
Bitboard king_jump_mask[NUM_SQUARES];
int8_t   jump_mid[NUM_SQUARES][NUM_SQUARES];
int8_t   diag_neighbour[NUM_SQUARES][NUM_DIRS];
int8_t   diag_jump[NUM_SQUARES][NUM_DIRS];
Bitboard row_mask[BOARD_SIZE];

uint64_t zobrist_piece[2][NUM_SQUARES];
uint64_t zobrist_king[NUM_SQUARES];
uint64_t zobrist_turn;

/* ------------------------------------------------------------------ */
/*  Internal helpers                                                   */
/* ------------------------------------------------------------------ */

static uint64_t xorshift64(uint64_t *state) {
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ------------------------------------------------------------------ */
/*  Initialisation                                                     */
/* ------------------------------------------------------------------ */

void bitboard_init(void) {
    /* ------- row masks ------- */
    for (int r = 0; r < BOARD_SIZE; r++) {
        row_mask[r] = 0;
        for (int i = 0; i < 4; i++)
            row_mask[r] |= (Bitboard)1u << (r * 4 + i);
    }

    /* ------- diagonal neighbours & jumps ------- */
    memset(diag_neighbour, -1, sizeof(diag_neighbour));
    memset(diag_jump, -1, sizeof(diag_jump));
    memset(jump_mid, -1, sizeof(jump_mid));

    /*
     * Direction offsets in (row, col):
     *   DIR_NE = (+1, +1)   DIR_NW = (+1, -1)
     *   DIR_SE = (-1, +1)   DIR_SW = (-1, -1)
     */
    static const int dr[NUM_DIRS] = { +1, +1, -1, -1 };
    static const int dc[NUM_DIRS] = { +1, -1, +1, -1 };

    for (int sq = 0; sq < NUM_SQUARES; sq++) {
        int r = sq_to_row(sq);
        int c = sq_to_col(sq);

        for (int d = 0; d < NUM_DIRS; d++) {
            int nr = r + dr[d];
            int nc = c + dc[d];
            if (nr < 0 || nr >= BOARD_SIZE || nc < 0 || nc >= BOARD_SIZE)
                continue;
            int nsq = rowcol_to_sq(nr, nc);
            if (nsq < 0) continue;
            diag_neighbour[sq][d] = (int8_t)nsq;

            /* jump: two steps in same direction */
            int jr = r + 2 * dr[d];
            int jc = c + 2 * dc[d];
            if (jr < 0 || jr >= BOARD_SIZE || jc < 0 || jc >= BOARD_SIZE)
                continue;
            int jsq = rowcol_to_sq(jr, jc);
            if (jsq < 0) continue;
            diag_jump[sq][d] = (int8_t)jsq;
            jump_mid[sq][jsq] = (int8_t)nsq;
        }
    }

    /* ------- move masks ------- */
    for (int sq = 0; sq < NUM_SQUARES; sq++) {
        man_move_mask[WHITE][sq] = 0;
        man_move_mask[BLACK][sq] = 0;
        king_move_mask[sq] = 0;
        man_jump_mask[WHITE][sq] = 0;
        man_jump_mask[BLACK][sq] = 0;
        king_jump_mask[sq] = 0;

        /* White men move NE, NW (increasing row) */
        for (int d = DIR_NE; d <= DIR_NW; d++) {
            int n = diag_neighbour[sq][d];
            if (n >= 0) man_move_mask[WHITE][sq] |= (Bitboard)1u << n;
            int j = diag_jump[sq][d];
            if (j >= 0) man_jump_mask[WHITE][sq] |= (Bitboard)1u << j;
        }

        /* Black men move SE, SW (decreasing row) */
        for (int d = DIR_SE; d <= DIR_SW; d++) {
            int n = diag_neighbour[sq][d];
            if (n >= 0) man_move_mask[BLACK][sq] |= (Bitboard)1u << n;
            int j = diag_jump[sq][d];
            if (j >= 0) man_jump_mask[BLACK][sq] |= (Bitboard)1u << j;
        }

        /* Kings move in all 4 directions */
        for (int d = 0; d < NUM_DIRS; d++) {
            int n = diag_neighbour[sq][d];
            if (n >= 0) king_move_mask[sq] |= (Bitboard)1u << n;
            int j = diag_jump[sq][d];
            if (j >= 0) king_jump_mask[sq] |= (Bitboard)1u << j;
        }
    }

    /* ------- Zobrist keys ------- */
    uint64_t rng_state = 0xDEADBEEF12345678ULL;
    for (int c = 0; c < 2; c++)
        for (int sq = 0; sq < NUM_SQUARES; sq++)
            zobrist_piece[c][sq] = xorshift64(&rng_state);
    for (int sq = 0; sq < NUM_SQUARES; sq++)
        zobrist_king[sq] = xorshift64(&rng_state);
    zobrist_turn = xorshift64(&rng_state);
}

/* ------------------------------------------------------------------ */
/*  Starting position                                                  */
/* ------------------------------------------------------------------ */

/*
 * Italian Checkers starting position:
 *   White men on rows 0, 1, 2 (squares 0-11)
 *   Black men on rows 5, 6, 7 (squares 20-31)
 */
GameState game_initial_state(void) {
    GameState s;
    memset(&s, 0, sizeof(s));

    /* White men: squares 0..11 */
    s.wp = 0x00000FFFu;
    /* Black men: squares 20..31 */
    s.bp = 0xFFF00000u;
    s.k  = 0;
    s.turn = WHITE;
    s.ply  = 0;
    s.no_progress = 0;

    /* Compute initial Zobrist hash */
    s.hash = 0;
    Bitboard tmp;
    int sq;
    FOR_EACH_BIT(s.wp, sq) {
        s.hash ^= zobrist_piece[WHITE][sq];
    }
    FOR_EACH_BIT(s.bp, sq) {
        s.hash ^= zobrist_piece[BLACK][sq];
    }
    /* turn is WHITE, so no zobrist_turn XOR */

    return s;
}
