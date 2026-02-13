/*
 * game.c - Game logic: make/unmake, result, material evaluation
 */

#include "game.h"
#include "bitboard.h"
#include "movegen.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Make move (with backup for undo)                                   */
/* ------------------------------------------------------------------ */

void game_make_move(GameState *s, const Move *m, MoveBackup *backup) {
    /* Save state for undo */
    backup->wp = s->wp;
    backup->bp = s->bp;
    backup->k  = s->k;
    backup->hash = s->hash;
    backup->no_progress = s->no_progress;

    game_make_move_fast(s, m);
}

void game_unmake_move(GameState *s, const Move *m, const MoveBackup *backup) {
    (void)m; /* backup is a complete snapshot of affected fields */
    s->wp = backup->wp;
    s->bp = backup->bp;
    s->k  = backup->k;
    s->hash = backup->hash;   /* restore exact pre-move hash */
    s->no_progress = backup->no_progress;
    s->turn = OPPONENT(s->turn);
    s->ply--;
}

/* ------------------------------------------------------------------ */
/*  Fast make move (no backup, used in rollouts)                       */
/* ------------------------------------------------------------------ */

void game_make_move_fast(GameState *s, const Move *m) {
    Color     c     = s->turn;
    Bitboard  from  = (Bitboard)1u << m->from;
    Bitboard  to    = (Bitboard)1u << m->to;
    Bitboard *own   = (c == WHITE) ? &s->wp : &s->bp;
    Bitboard *opp   = (c == WHITE) ? &s->bp : &s->wp;
    Color     opp_c = OPPONENT(c);
    bool      was_king = (s->k & from) != 0;

    /* Update Zobrist: remove piece from origin */
    s->hash ^= zobrist_piece[c][m->from];
    if (was_king) s->hash ^= zobrist_king[m->from];

    /* Move the piece */
    *own &= ~from;
    *own |= to;

    /* Handle king bit */
    if (was_king) {
        s->k &= ~from;
        s->k |= to;
    }

    /* Remove captured pieces */
    for (int i = 0; i < m->n_captures; i++) {
        int cap_sq = m->captures[i];
        Bitboard cap_bit = (Bitboard)1u << cap_sq;
        s->hash ^= zobrist_piece[opp_c][cap_sq];
        if (s->k & cap_bit) {
            s->hash ^= zobrist_king[cap_sq];
            s->k &= ~cap_bit;
        }
        *opp &= ~cap_bit;
    }

    /* Promotion */
    if (m->promotion && !was_king) {
        s->k |= to;
        s->hash ^= zobrist_king[m->to];
    }

    /* Update Zobrist: place piece at destination */
    bool is_now_king = (s->k & to) != 0;
    s->hash ^= zobrist_piece[c][m->to];
    if (is_now_king) s->hash ^= zobrist_king[m->to];

    /* Progress tracking */
    if (m->n_captures > 0 || m->promotion)
        s->no_progress = 0;
    else
        s->no_progress++;

    s->turn = opp_c;
    s->ply++;
    s->hash ^= zobrist_turn;
}

/* ------------------------------------------------------------------ */
/*  Game result detection                                              */
/* ------------------------------------------------------------------ */

GameResult game_result(const GameState *s) {
    /* Side to move has no pieces → loses */
    Bitboard own = (s->turn == WHITE) ? s->wp : s->bp;
    if (own == 0) {
        return (s->turn == WHITE) ? RESULT_BLACK : RESULT_WHITE;
    }

    /* Side to move has no legal moves → loses */
    if (count_moves(s) == 0) {
        return (s->turn == WHITE) ? RESULT_BLACK : RESULT_WHITE;
    }

    /* 80 half-move no-progress rule → draw */
    if (s->no_progress >= 80) {
        return RESULT_DRAW;
    }

    /* Total ply cap → draw (prevents infinite games) */
    if (s->ply >= MAX_PLY) {
        return RESULT_DRAW;
    }

    /* Endgame draw detection: 1K vs 1K */
    int wp_count = popcount32(s->wp);
    int bp_count = popcount32(s->bp);
    if (wp_count == 1 && bp_count == 1 &&
        (s->k & s->wp) && (s->k & s->bp)) {
        return RESULT_DRAW;
    }

    return RESULT_ONGOING;
}

/* ------------------------------------------------------------------ */
/*  Material evaluation (for rollout cutoff)                           */
/* ------------------------------------------------------------------ */

int game_material_score(const GameState *s) {
    int w_men   = popcount32(s->wp & ~s->k);
    int w_kings = popcount32(s->wp & s->k);
    int b_men   = popcount32(s->bp & ~s->k);
    int b_kings = popcount32(s->bp & s->k);

    /* Kings worth ~1.5 men in Italian checkers endgame */
    return (w_men * 100 + w_kings * 150) - (b_men * 100 + b_kings * 150);
}

/* ------------------------------------------------------------------ */
/*  Hash history for repetition detection                              */
/* ------------------------------------------------------------------ */

void hash_history_init(HashHistory *h) {
    h->count = 0;
}

void hash_history_push(HashHistory *h, uint64_t hash) {
    if (h->count < HASH_HISTORY_SIZE)
        h->hashes[h->count++] = hash;
}

void hash_history_pop(HashHistory *h) {
    if (h->count > 0)
        h->count--;
}

int hash_history_count(const HashHistory *h, uint64_t hash) {
    int cnt = 0;
    for (int i = 0; i < h->count; i++)
        if (h->hashes[i] == hash)
            cnt++;
    return cnt;
}
