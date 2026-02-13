/*
 * movegen.h - Move generation interface
 */

#ifndef MOVEGEN_H
#define MOVEGEN_H

#include "types.h"

/*
 * Generate all legal moves for the current side to move.
 * Enforces Italian capture priority rules:
 *   1. Must capture maximum number of pieces
 *   2. Must prefer king captures over man captures
 *   3. Among ties, prefer capturing most opponent kings
 *   4. Among ties, prefer earliest king capture in sequence
 *
 * Returns the number of legal moves written into `moves`.
 */
int generate_moves(const GameState *s, Move moves[MAX_MOVES]);

/*
 * Quick check: does the side to move have any captures?
 * Faster than full generation when you only need a boolean.
 */
bool has_captures(const GameState *s);

/*
 * Count legal moves without generating them (used in MCTS).
 */
int count_moves(const GameState *s);

#endif /* MOVEGEN_H */
