/*
 * mcts.c - Monte Carlo Tree Search with UCB1 and PUCT policies
 *
 * Key optimisations:
 *   - Arena pool allocator (no malloc/free during search)
 *   - No game-state cloning during rollout (stack copy at rollout start)
 *   - Fast rollout with bitboard move generation
 *   - Anytime: polls wall-clock every 128 iterations
 *   - Endgame adjudication in rollout
 */

#include "mcts.h"
#include "bitboard.h"
#include "movegen.h"
#include "game.h"
#include "rng.h"

#include <math.h>
#include <string.h>
#include <stdio.h>
#include <float.h>

#ifdef _WIN32
#include <windows.h>
static double get_time_sec(void) {
    static LARGE_INTEGER freq = {0};
    if (freq.QuadPart == 0) QueryPerformanceFrequency(&freq);
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}
#else
#include <time.h>
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
#endif

/* ------------------------------------------------------------------ */
/*  Default configuration                                              */
/* ------------------------------------------------------------------ */

MCTSConfig mcts_default_config(void) {
    MCTSConfig cfg;
    cfg.policy      = POLICY_UCB1;
    cfg.cp          = 1.41421356;    /* sqrt(2) */
    cfg.cpuct       = 1.0;
    cfg.time_limit  = 1.0;
    cfg.max_rollout = 200;
    cfg.rollout_material_cutoff = 3;
    cfg.pool_capacity = 4000000;      /* ~4M nodes ≈ 256MB */
    return cfg;
}


/* ------------------------------------------------------------------ */
/*  Node operations                                                    */
/* ------------------------------------------------------------------ */

static MCTSNode *node_alloc(MCTSSearch *search) {
    return (MCTSNode *)pool_alloc(&search->node_pool);
}

static MCTSNode *node_alloc_children(MCTSSearch *search, int count) {
    return (MCTSNode *)pool_alloc_array(&search->node_pool, (size_t)count);
}

static void node_init(MCTSNode *node, MCTSNode *parent, const Move *move) {
    node->w = 0.0f;
    node->n = 0;
    node->n_children = 0;
    node->n_untried  = 0;
    node->children   = NULL;
    node->parent     = parent;
    if (move)
        node->move = *move;
}

/* ------------------------------------------------------------------ */
/*  Selection policies                                                 */
/* ------------------------------------------------------------------ */

static double ucb1_score(const MCTSNode *child, double cp, double log_parent_n) {
    if (child->n == 0) return DBL_MAX;
    double exploitation = (double)child->w / (double)child->n;
    double exploration  = cp * sqrt(log_parent_n / (double)child->n);
    return exploitation + exploration;
}

static double puct_score(const MCTSNode *child, double cpuct,
                         double sqrt_parent_n, int n_actions) {
    double prior = 1.0 / (double)n_actions;  /* uniform prior */
    double exploitation = (child->n > 0) ? ((double)child->w / (double)child->n) : 0.0;
    double exploration  = cpuct * prior * sqrt_parent_n / (1.0 + (double)child->n);
    return exploitation + exploration;
}

static MCTSNode *select_child(const MCTSSearch *search, MCTSNode *node) {
    MCTSNode *best = NULL;
    double best_score = -DBL_MAX;

    if (search->cfg.policy == POLICY_UCB1) {
        double log_N = log((double)node->n + 1.0);
        for (int i = 0; i < node->n_children; i++) {
            double score = ucb1_score(&node->children[i], search->cfg.cp, log_N);
            if (score > best_score) {
                best_score = score;
                best = &node->children[i];
            }
        }
    } else {
        /* PUCT */
        double sqrt_N = sqrt((double)node->n);
        int n_actions = node->n_children;
        for (int i = 0; i < node->n_children; i++) {
            double score = puct_score(&node->children[i], search->cfg.cpuct,
                                       sqrt_N, n_actions);
            if (score > best_score) {
                best_score = score;
                best = &node->children[i];
            }
        }
    }

    return best;
}

/* ------------------------------------------------------------------ */
/*  Expansion                                                          */
/* ------------------------------------------------------------------ */

static bool expand_node(MCTSSearch *search, MCTSNode *node,
                        const GameState *state) {
    Move moves[MAX_MOVES];
    int n = generate_moves(state, moves);
    if (n == 0) return false;

    /* Allocate children array from pool contiguously */
    MCTSNode *children = node_alloc_children(search, n);
    if (!children) return false;  /* pool exhausted */

    for (int i = 0; i < n; i++) {
        node_init(&children[i], node, &moves[i]);
    }

    node->children   = children;
    node->n_children = (int16_t)n;
    node->n_untried  = (int16_t)n;
    return true;
}

/* ------------------------------------------------------------------ */
/*  Rollout (simulation)                                               */
/* ------------------------------------------------------------------ */

/*
 * Fast rollout: no state cloning (uses stack copy), heuristic termination.
 *
 * Move selection uses a BIASED policy instead of pure random:
 *   - Captures are already mandatory (generate_moves enforces them), so
 *     when the move list contains captures all moves are captures — no
 *     extra logic is needed to prefer them.
 *   - Among quiet moves, forward steps (toward promotion) are weighted 3x
 *     vs backward steps (weight 1). King moves are weighted uniformly.
 *
 * This lightweight bias dramatically reduces the draw rate caused by
 * fully-random rollouts wandering without progress, as required by the spec.
 *
 * Returns score from WHITE's perspective: 1.0 = white wins, 0.0 = black, 0.5 = draw
 */

/* Weighted move selection for rollout.
 * Assigns each move an integer weight and samples proportionally using
 * a single random number — O(n) with no extra allocation. */
static int rollout_pick_move(uint64_t *rng, const Move *moves, int n,
                             Color turn) {
    /* Fast path: nothing to choose */
    if (n == 1) return 0;

    /* If moves are captures (n_captures > 0 on first move) use uniform
     * selection — all captures are already priority-filtered by generate_moves,
     * so they are equally desirable at the rollout level. */
    if (moves[0].n_captures > 0)
        return rand_int(rng, n);

    /* Quiet moves: assign weights.
     *   forward step  → weight 3   (toward promotion row)
     *   backward step → weight 1
     *   king move     → weight 2   (neutral: kings don't have a clear direction)
     */
    int weights[MAX_MOVES];
    int total = 0;
    for (int i = 0; i < n; i++) {
        int w;
        if (moves[i].is_king_move) {
            w = 2;
        } else {
            int from_row = sq_to_row(moves[i].from);
            int to_row   = sq_to_row(moves[i].to);
            bool forward = (turn == WHITE) ? (to_row > from_row)
                                           : (to_row < from_row);
            w = forward ? 3 : 1;
        }
        weights[i] = w;
        total += w;
    }

    /* Sample: pick a threshold in [0, total) then walk the weight array */
    int r = (int)(xorshift64(rng) % (uint64_t)total);
    int acc = 0;
    for (int i = 0; i < n - 1; i++) {
        acc += weights[i];
        if (r < acc) return i;
    }
    return n - 1;
}

static float rollout(MCTSSearch *search, GameState state) {
    Move moves[MAX_MOVES];
    int depth = 0;
    int max_depth = search->cfg.max_rollout;

    while (depth < max_depth) {
        /* Quick game-over check */
        Bitboard own = (state.turn == WHITE) ? state.wp : state.bp;
        if (own == 0) {
            /* Side to move has no pieces */
            return (state.turn == WHITE) ? 0.0f : 1.0f;
        }

        int n = generate_moves(&state, moves);
        if (n == 0) {
            /* No legal moves */
            return (state.turn == WHITE) ? 0.0f : 1.0f;
        }

        /* Endgame adjudication: very few pieces left */
        int total = popcount32(state.wp) + popcount32(state.bp);
        if (total <= search->cfg.rollout_material_cutoff) {
            /* Use material heuristic */
            int score = game_material_score(&state);
            if (score > 50) return 1.0f;
            if (score < -50) return 0.0f;
            return 0.5f;
        }

        /* No-progress cutoff (matches game_result rule) */
        if (state.no_progress >= NO_PROGRESS_DRAW) {
            return 0.5f;
        }

        /* Pick move using biased rollout policy */
        int idx = rollout_pick_move(&search->rng_state, moves, n, state.turn);
        game_make_move_fast(&state, &moves[idx]);
        depth++;
    }

    /* Exceeded max depth: evaluate by material */
    int score = game_material_score(&state);
    if (score > 30) return 1.0f;
    if (score < -30) return 0.0f;
    return 0.5f;
}

/* ------------------------------------------------------------------ */
/*  Backpropagation                                                    */
/* ------------------------------------------------------------------ */
static void backpropagate(MCTSNode *node, float result, Color root_turn) {
    /*
     * Walk from leaf back to root in two O(depth) passes (total O(2*depth)):
     *   Pass 1: count depth to determine the leaf's mover parity.
     *   Pass 2: walk up, updating n and w with alternating perspective.
     *
     * `result` is from WHITE's perspective (1.0 = white wins, 0.0 = black).
     * Each node stores w from the perspective of the side that chose the move
     * leading INTO this node (i.e. the parent's turn at that moment).
     */
    float root_score = (root_turn == WHITE) ? result : (1.0f - result);

    /* Pass 1: count depth from root */
    int depth = 0;
    for (MCTSNode *p = node; p->parent != NULL; p = p->parent)
        depth++;

    /* Pass 2: update — flip perspective at each level */
    bool mover_is_root = ((depth % 2) == 1);
    MCTSNode *cur = node;
    while (cur != NULL) {
        cur->n++;
        if (cur->parent != NULL) {
            cur->w += mover_is_root ? root_score : (1.0f - root_score);
        }
        mover_is_root = !mover_is_root;
        cur = cur->parent;
    }
}


/* ------------------------------------------------------------------ */
/*  Main MCTS search loop                                              */
/* ------------------------------------------------------------------ */

Move mcts_search(MCTSSearch *search, const GameState *state, const HashHistory *history) {
    double start_time = get_time_sec();
    double deadline = start_time + search->cfg.time_limit;

    /* Reset pool & create root */
    pool_reset(&search->node_pool);
    search->root = node_alloc(search);
    node_init(search->root, NULL, NULL);
    search->total_simulations = 0;
    search->total_rollout_moves = 0;

    /* Expand root immediately */
    if (!expand_node(search, search->root, state)) {
        /* No legal moves at root — shouldn't happen if game not over */
        Move null_move;
        memset(&null_move, 0, sizeof(null_move));
        return null_move;
    }

    /* Initialize RNG */
    if (search->rng_state == 0)
        search->rng_state = 0xABCDEF0123456789ULL;

    uint64_t iter = 0;
    const int CHECK_INTERVAL = 128;

    while (1) {
        /* Time check every CHECK_INTERVAL iterations */
        if ((iter & (CHECK_INTERVAL - 1)) == 0 && iter > 0) {
            if (get_time_sec() >= deadline) break;
        }

        /* --- Selection --- */
        MCTSNode *node = search->root;
        GameState sim = *state;  /* one copy per iteration */

        /* Track path hashes to detect repetitions during selection */
        uint64_t path_hashes[MAX_PLY];
        int path_len = 0;
        path_hashes[path_len++] = sim.hash;
        bool is_repetition_draw = false;

        while (node->n_children > 0 && node->n_untried == 0) {
            node = select_child(search, node);
            game_make_move_fast(&sim, &node->move);

            if (path_len < MAX_PLY) {
                path_hashes[path_len++] = sim.hash;
            }

            /* Count occurrences of sim.hash */
            int rep_count = 0;
            if (history) {
                rep_count += hash_history_count(history, sim.hash);
            }
            for (int i = 0; i < path_len; i++) {
                if (path_hashes[i] == sim.hash) {
                    rep_count++;
                }
            }

            /* If it has appeared 3 times in total (real history + current path), it's a draw */
            if (rep_count >= 3) {
                is_repetition_draw = true;
                break;
            }
        }

        float result = 0.5f;

        if (is_repetition_draw) {
            result = 0.5f;
            backpropagate(node, result, state->turn);
            iter++;
            search->total_simulations++;
            continue;
        }

        /* --- Expansion --- */
        if (node->n > 0 && node->n_children == 0) {
            /* Try to expand if not a terminal node */
            GameResult res = game_result(&sim);
            if (res == RESULT_ONGOING) {
                if (expand_node(search, node, &sim)) {
                    /* Pick first untried child */
                    node->n_untried = node->n_children;  /* already set, but be safe */
                    int idx = rand_int(&search->rng_state, node->n_children);
                    node = &node->children[idx];
                    node->parent->n_untried--;
                    game_make_move_fast(&sim, &node->move);
                }
            } else {
                /* Terminal node: score directly */
                float score;
                switch (res) {
                    case RESULT_WHITE: score = 1.0f; break;
                    case RESULT_BLACK: score = 0.0f; break;
                    default:           score = 0.5f; break;
                }
                backpropagate(node, score, state->turn);
                iter++;
                search->total_simulations++;
                continue;
            }
        } else if (node->n_untried > 0) {
            /* Pick a random untried child */
            int untried_idx = rand_int(&search->rng_state, node->n_untried);
            int cnt = 0;
            for (int i = 0; i < node->n_children; i++) {
                if (node->children[i].n == 0) {
                    if (cnt == untried_idx) {
                        node = &node->children[i];
                        node->parent->n_untried--;
                        game_make_move_fast(&sim, &node->move);
                        break;
                    }
                    cnt++;
                }
            }
        }

        /* --- Simulation (rollout) --- */
        result = rollout(search, sim);

        /* --- Backpropagation --- */
        backpropagate(node, result, state->turn);

        iter++;
        search->total_simulations++;
    }

    /* --- Choose best move: most visits --- */
    MCTSNode *best = NULL;
    uint32_t best_visits = 0;
    for (int i = 0; i < search->root->n_children; i++) {
        MCTSNode *child = &search->root->children[i];
        if (child->n > best_visits) {
            best_visits = child->n;
            best = child;
        }
    }

    double elapsed = get_time_sec() - start_time;

    /* Print search stats */
    double sps = (elapsed > 0) ? (double)search->total_simulations / elapsed : 0;
    fprintf(stderr, "[MCTS] %llu sims in %.3fs (%.0f sims/s), pool: %.1f%%\n",
            (unsigned long long)search->total_simulations, elapsed, sps,
            pool_usage_pct(&search->node_pool));

    if (best) {
        float wr = (best->n > 0) ? best->w / (float)best->n : 0.0f;
        fprintf(stderr, "[MCTS] Best move: %d->%d, visits=%u, winrate=%.1f%%\n",
                best->move.from, best->move.to, best->n, wr * 100.0f);
        return best->move;
    }

    Move null_move;
    memset(&null_move, 0, sizeof(null_move));
    return null_move;
}

/* ------------------------------------------------------------------ */
/*  Init / Destroy                                                     */
/* ------------------------------------------------------------------ */

bool mcts_init(MCTSSearch *search, const MCTSConfig *cfg) {
    search->cfg = *cfg;
    search->root = NULL;
    search->total_simulations = 0;
    search->total_rollout_moves = 0;
    search->rng_state = 0xDEADBEEF42ULL;

    return pool_init(&search->node_pool, sizeof(MCTSNode), cfg->pool_capacity);
}

void mcts_destroy(MCTSSearch *search) {
    pool_destroy(&search->node_pool);
    search->root = NULL;
}

/* ------------------------------------------------------------------ */
/*  Statistics                                                         */
/* ------------------------------------------------------------------ */

MCTSStats mcts_get_stats(const MCTSSearch *search) {
    MCTSStats stats;
    memset(&stats, 0, sizeof(stats));
    stats.simulations = search->total_simulations;
    stats.pool_usage  = pool_usage_pct(&search->node_pool);

    if (search->root) {
        MCTSNode *best = NULL;
        uint32_t max_n = 0;
        for (int i = 0; i < search->root->n_children; i++) {
            if (search->root->children[i].n > max_n) {
                max_n = search->root->children[i].n;
                best = &search->root->children[i];
            }
        }
        if (best) {
            stats.best_visits  = best->n;
            stats.best_winrate = (best->n > 0) ? best->w / (float)best->n : 0.0f;
        }
    }
    return stats;
}
