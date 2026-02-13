/*
 * mcts.h - Monte Carlo Tree Search with UCB1 and PUCT policies
 */

#ifndef MCTS_H
#define MCTS_H

#include "types.h"
#include "pool.h"

/* ------------------------------------------------------------------ */
/*  MCTS Node                                                          */
/* ------------------------------------------------------------------ */

typedef struct MCTSNode {
    float              w;              /* cumulative score (from this node's perspective) */
    uint32_t           n;              /* visit count                    */
    Move               move;           /* move that led to this node     */
    int16_t            n_children;     /* number of children             */
    int16_t            n_untried;      /* children not yet expanded      */
    struct MCTSNode   *children;       /* array allocated from pool      */
    struct MCTSNode   *parent;
} MCTSNode;

/* ------------------------------------------------------------------ */
/*  MCTS Configuration                                                 */
/* ------------------------------------------------------------------ */

typedef struct {
    PolicyType  policy;         /* UCB1 or PUCT                        */
    double      cp;             /* exploration constant for UCB1       */
    double      cpuct;          /* exploration constant for PUCT       */
    double      time_limit;     /* search budget in seconds            */
    int         max_rollout;    /* max half-moves per rollout          */
    int         rollout_material_cutoff; /* piece threshold for early eval */
    /* Pool sizing */
    size_t      pool_capacity;  /* max nodes in pool                   */
} MCTSConfig;

/* Default configuration */
MCTSConfig mcts_default_config(void);

/* ------------------------------------------------------------------ */
/*  MCTS Search context                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    MCTSConfig  cfg;
    Pool        node_pool;
    MCTSNode   *root;
    uint64_t    total_simulations;
    uint64_t    total_rollout_moves;
    /* XorShift RNG state for rollouts */
    uint64_t    rng_state;
} MCTSSearch;

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/* Initialise search context (allocates pool) */
bool mcts_init(MCTSSearch *search, const MCTSConfig *cfg);

/* Run MCTS from given state; returns best move */
Move mcts_search(MCTSSearch *search, const GameState *state);

/* Free all resources */
void mcts_destroy(MCTSSearch *search);

/* ------------------------------------------------------------------ */
/*  Statistics (for analysis)                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t simulations;
    double   sims_per_sec;
    double   elapsed;
    float    best_winrate;
    uint32_t best_visits;
    double   pool_usage;
} MCTSStats;

MCTSStats mcts_get_stats(const MCTSSearch *search);

#endif /* MCTS_H */
