/*
 * movegen.c - Move generation with Italian capture priority rules
 *
 * Italian capture priorities (in order):
 *   1. Must capture the maximum number of pieces
 *   2. If equal, must capture with a king rather than a man
 *   3. If equal, must capture the maximum number of opponent kings
 *   4. If equal, must capture where a king is encountered earliest
 */

#include "movegen.h"
#include "bitboard.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal capture-DFS state                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    const GameState *state;
    Color           color;
    Bitboard        own;        /* pieces of the moving side           */
    Bitboard        opp;        /* pieces of the opponent              */
    Bitboard        occupied;
    Move            captures[MAX_MOVES]; /* collected capture sequences */
    int             n_captures;
    /* current DFS path */
    Move            path;
    Bitboard        removed;    /* bitmask of pieces removed so far    */
    /* best priority values found so far */
    int             best_count;
    bool            best_is_king;
    int             best_king_caps;
    int             best_first_king;
} CaptureCtx;

/* ------------------------------------------------------------------ */
/*  Forward declaration                                                */
/* ------------------------------------------------------------------ */

static void capture_dfs(CaptureCtx *ctx, int sq, bool is_king, int depth);
static void try_record_capture(CaptureCtx *ctx);
static bool is_better_capture(const CaptureCtx *ctx, const Move *m);

/* ------------------------------------------------------------------ */
/*  Italian-priority comparison                                        */
/* ------------------------------------------------------------------ */

static bool is_better_capture(const CaptureCtx *ctx, const Move *m) {
    /* 1. More captures wins */
    if (m->n_captures > ctx->best_count) return true;
    if (m->n_captures < ctx->best_count) return false;
    /* 2. King move beats man move */
    if (m->is_king_move && !ctx->best_is_king) return true;
    if (!m->is_king_move && ctx->best_is_king) return false;
    /* 3. More king captures wins */
    if (m->king_captures > ctx->best_king_caps) return true;
    if (m->king_captures < ctx->best_king_caps) return false;
    /* 4. Earlier first king capture wins (lower index = earlier) */
    if (m->king_captures > 0 && m->first_king_idx < ctx->best_first_king)
        return true;
    return false;
}

/* ------------------------------------------------------------------ */
/*  Record a capture sequence if it passes priority filtering          */
/* ------------------------------------------------------------------ */

static void try_record_capture(CaptureCtx *ctx) {
    Move *m = &ctx->path;
    if (m->n_captures == 0) return;

    if (is_better_capture(ctx, m)) {
        /* This is strictly better: reset list */
        ctx->best_count      = m->n_captures;
        ctx->best_is_king    = m->is_king_move;
        ctx->best_king_caps  = m->king_captures;
        ctx->best_first_king = (m->king_captures > 0) ? m->first_king_idx : 99;
        ctx->n_captures = 0;
        ctx->captures[ctx->n_captures++] = *m;
    }
    else if (m->n_captures == ctx->best_count &&
             m->is_king_move == ctx->best_is_king &&
             m->king_captures == ctx->best_king_caps &&
             ((m->king_captures == 0) ||
              (m->first_king_idx == ctx->best_first_king))) {
        /* Equal priority: add to list */
        if (ctx->n_captures < MAX_MOVES)
            ctx->captures[ctx->n_captures++] = *m;
    }
}

/* ------------------------------------------------------------------ */
/*  DFS capture search for a single piece                              */
/* ------------------------------------------------------------------ */

static void capture_dfs(CaptureCtx *ctx, int sq, bool is_king, int depth) {
    bool found_jump = false;
    int start_dir = 0, end_dir = NUM_DIRS;

    /* Men can only capture forward in Italian checkers */
    if (!is_king) {
        if (ctx->color == WHITE) {
            start_dir = DIR_NE;
            end_dir   = DIR_NW + 1; /* NE and NW only */
        } else {
            start_dir = DIR_SE;
            end_dir   = DIR_SW + 1; /* SE and SW only */
        }
    }

    for (int d = start_dir; d < end_dir; d++) {
        int mid = diag_neighbour[sq][d];
        int land = diag_jump[sq][d];
        if (mid < 0 || land < 0) continue;

        Bitboard mid_bit  = (Bitboard)1u << mid;
        Bitboard land_bit = (Bitboard)1u << land;

        /* Must jump over opponent, not already removed, land on empty.
         * During multi-capture, the moving piece has left its origin square,
         * so we exclude it from occupation check. Also exclude removed pieces. */
        if (!(ctx->opp & mid_bit)) continue;
        if (ctx->removed & mid_bit) continue;
        Bitboard dynamic_occ = ctx->occupied & ~ctx->removed
                               & ~((Bitboard)1u << ctx->path.from);
        if (dynamic_occ & land_bit) continue;

        /* Italian rule: a man cannot capture a king */
        if (!is_king && (ctx->state->k & mid_bit)) continue;

        found_jump = true;

        /* record this capture step */
        bool captured_king = (ctx->state->k & mid_bit) != 0;
        ctx->path.captures[depth] = (uint8_t)mid;
        ctx->path.n_captures = (uint8_t)(depth + 1);
        ctx->path.to = (uint8_t)land;
        if (captured_king) {
            ctx->path.king_captures++;
            if (ctx->path.first_king_idx == 255)
                ctx->path.first_king_idx = (uint8_t)depth;
        }
        ctx->removed |= mid_bit;

        /* Check promotion: man reaches last row ⟹ stops, becomes king */
        bool promotes = false;
        if (!is_king) {
            Bitboard promo = (ctx->color == WHITE) ? PROMOTION_ROW_WHITE : PROMOTION_ROW_BLACK;
            if (land_bit & promo) {
                promotes = true;
                ctx->path.promotion = true;
            }
        }

        if (promotes) {
            /* In Italian checkers, man that promotes STOPS – cannot continue capturing */
            try_record_capture(ctx);
        } else {
            /* Recurse */
            capture_dfs(ctx, land, is_king, depth + 1);
        }

        /* undo */
        ctx->removed &= ~mid_bit;
        if (captured_king) {
            ctx->path.king_captures--;
            if (ctx->path.first_king_idx == (uint8_t)depth)
                ctx->path.first_king_idx = 255;
        }
        ctx->path.promotion = false;
    }

    if (!found_jump && depth > 0) {
        /* Leaf of capture sequence */
        try_record_capture(ctx);
    }
}

/* ------------------------------------------------------------------ */
/*  Top-level move generation                                          */
/* ------------------------------------------------------------------ */

int generate_moves(const GameState *s, Move moves[MAX_MOVES]) {
    CaptureCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.state = s;
    ctx.color = s->turn;
    ctx.own = (s->turn == WHITE) ? s->wp : s->bp;
    ctx.opp = (s->turn == WHITE) ? s->bp : s->wp;
    ctx.occupied = s->wp | s->bp;
    ctx.best_count = 0;
    ctx.best_is_king = false;
    ctx.best_king_caps = 0;
    ctx.best_first_king = 99;

    /* --- Phase 1: Generate all capture sequences --- */
    int sq;
    Bitboard pieces = ctx.own;
    FOR_EACH_BIT(pieces, sq) {
        bool is_king = (s->k & ((Bitboard)1u << sq)) != 0;

        memset(&ctx.path, 0, sizeof(ctx.path));
        ctx.path.from = (uint8_t)sq;
        ctx.path.is_king_move = is_king;
        ctx.path.first_king_idx = 255;
        ctx.removed = 0;

        capture_dfs(&ctx, sq, is_king, 0);
    }

    if (ctx.n_captures > 0) {
        /* Copy best-priority captures to output */
        int n = ctx.n_captures;
        if (n > MAX_MOVES) n = MAX_MOVES;
        memcpy(moves, ctx.captures, n * sizeof(Move));
        return n;
    }

    /* --- Phase 2: No captures ⟹ generate quiet moves --- */
    int n_moves = 0;
    pieces = ctx.own;
    FOR_EACH_BIT(pieces, sq) {
        bool is_king = (s->k & ((Bitboard)1u << sq)) != 0;
        Bitboard targets;

        if (is_king) {
            targets = king_move_mask[sq] & ~ctx.occupied;
        } else {
            targets = man_move_mask[s->turn][sq] & ~ctx.occupied;
        }

        int to;
        FOR_EACH_BIT(targets, to) {
            if (n_moves >= MAX_MOVES) break;
            Move *m = &moves[n_moves++];
            memset(m, 0, sizeof(Move));
            m->from = (uint8_t)sq;
            m->to   = (uint8_t)to;
            m->is_king_move = is_king;

            /* Check promotion */
            if (!is_king) {
                Bitboard promo = (s->turn == WHITE) ? PROMOTION_ROW_WHITE : PROMOTION_ROW_BLACK;
                if (((Bitboard)1u << to) & promo)
                    m->promotion = true;
            }
        }
    }

    return n_moves;
}

/* ------------------------------------------------------------------ */
/*  Utility functions                                                  */
/* ------------------------------------------------------------------ */

bool has_captures(const GameState *s) {
    Color c = s->turn;
    Bitboard own = (c == WHITE) ? s->wp : s->bp;
    Bitboard opp = (c == WHITE) ? s->bp : s->wp;
    Bitboard occupied = s->wp | s->bp;

    int sq;
    FOR_EACH_BIT(own, sq) {
        bool is_king = (s->k & ((Bitboard)1u << sq)) != 0;
        int start_dir, end_dir;

        if (is_king) {
            start_dir = 0; end_dir = NUM_DIRS;
        } else if (c == WHITE) {
            start_dir = DIR_NE; end_dir = DIR_NW + 1;
        } else {
            start_dir = DIR_SE; end_dir = DIR_SW + 1;
        }

        for (int d = start_dir; d < end_dir; d++) {
            int mid = diag_neighbour[sq][d];
            int land = diag_jump[sq][d];
            if (mid < 0 || land < 0) continue;
            Bitboard mid_bit  = (Bitboard)1u << mid;
            Bitboard land_bit = (Bitboard)1u << land;
            if (!(opp & mid_bit)) continue;
            if (occupied & land_bit) continue;
            /* Italian rule: man cannot capture king */
            if (!is_king && (s->k & mid_bit)) continue;
            return true;
        }
    }
    return false;
}

int count_moves(const GameState *s) {
    Move tmp[MAX_MOVES];
    return generate_moves(s, tmp);
}
