/*
 * game.h - Game logic: make/unmake move, result detection
 */

#ifndef GAME_H
#define GAME_H

#include "types.h"

/* Lightweight backup for unmake (avoids full state copy) */
typedef struct {
    Bitboard wp, bp, k;
    uint64_t hash;
    int      no_progress;
} MoveBackup;

/* Apply a move in-place; fills backup for undo */
void game_make_move(GameState *s, const Move *m, MoveBackup *backup);

/* Restore state from backup */
void game_unmake_move(GameState *s, const Move *m, const MoveBackup *backup);

/* Apply a move in-place without backup (rollout fast path) */
void game_make_move_fast(GameState *s, const Move *m);

/* Check game result: ONGOING, WHITE, BLACK, or DRAW */
GameResult game_result(const GameState *s);

/* Simple material score: positive = white advantage */
int game_material_score(const GameState *s);

/* ------------------------------------------------------------------ */
/*  Repetition detection (simple hash history)                         */
/* ------------------------------------------------------------------ */

#define HASH_HISTORY_SIZE 512

typedef struct {
    uint64_t hashes[HASH_HISTORY_SIZE];
    int      count;
} HashHistory;

void hash_history_init(HashHistory *h);
void hash_history_push(HashHistory *h, uint64_t hash);
void hash_history_pop(HashHistory *h);
int  hash_history_count(const HashHistory *h, uint64_t hash);

#endif /* GAME_H */
