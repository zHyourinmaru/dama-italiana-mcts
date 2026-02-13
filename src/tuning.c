/*
 * tuning.c - Self-play, round-robin tournaments, genetic algorithm
 */

#include "tuning.h"
#include "bitboard.h"
#include "movegen.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Fast RNG for GA                                                    */
/* ------------------------------------------------------------------ */

static uint64_t ga_rng_state = 0x12345678ABCDEF01ULL;

static double rand_double(void) {
    ga_rng_state ^= ga_rng_state << 13;
    ga_rng_state ^= ga_rng_state >> 7;
    ga_rng_state ^= ga_rng_state << 17;
    return (double)(ga_rng_state & 0xFFFFFFFF) / (double)0xFFFFFFFF;
}

/* ------------------------------------------------------------------ */
/*  Self-play: one complete game between two MCTS configs              */
/* ------------------------------------------------------------------ */

SelfPlayResult selfplay_game(const MCTSConfig *white_cfg,
                             const MCTSConfig *black_cfg,
                             bool verbose) {
    SelfPlayResult result;
    memset(&result, 0, sizeof(result));

    MCTSSearch white_search, black_search;
    mcts_init(&white_search, white_cfg);
    mcts_init(&black_search, black_cfg);

    GameState state = game_initial_state();
    HashHistory history;
    hash_history_init(&history);
    hash_history_push(&history, state.hash);

    int max_game_ply = MAX_PLY;
    GameResult res = RESULT_ONGOING;

    while (res == RESULT_ONGOING && state.ply < max_game_ply) {
        MCTSSearch *current_search = (state.turn == WHITE) ? &white_search : &black_search;

        Move best = mcts_search(current_search, &state);

        if (state.turn == WHITE)
            result.white_sims += current_search->total_simulations;
        else
            result.black_sims += current_search->total_simulations;

        MoveBackup backup;
        game_make_move(&state, &best, &backup);
        hash_history_push(&history, state.hash);
        result.total_moves++;

        if (verbose && result.total_moves % 20 == 0) {
            fprintf(stderr, "  [selfplay] ply %d, W=%d B=%d K=%d\n",
                    state.ply, popcount32(state.wp), popcount32(state.bp),
                    popcount32(state.k));
        }

        /* Check for triple repetition */
        if (hash_history_count(&history, state.hash) >= 3) {
            res = RESULT_DRAW;
        } else {
            res = game_result(&state);
        }
    }

    if (res == RESULT_ONGOING) res = RESULT_DRAW; /* game too long */

    result.result = res;

    mcts_destroy(&white_search);
    mcts_destroy(&black_search);

    return result;
}

/* ------------------------------------------------------------------ */
/*  Round-robin tournament                                             */
/* ------------------------------------------------------------------ */

void tournament_run(Tournament *t) {
    printf("========================================\n");
    printf(" Round-Robin Tournament (%d configs, %d games/pair)\n",
           t->n_configs, t->games_per_pair);
    printf("========================================\n\n");

    /* Reset results */
    for (int i = 0; i < t->n_configs; i++) {
        t->configs[i].wins = 0;
        t->configs[i].losses = 0;
        t->configs[i].draws = 0;
        t->configs[i].games = 0;
        t->configs[i].avg_sims = 0;
        t->configs[i].score = 0;
    }

    int total_matches = 0;
    for (int i = 0; i < t->n_configs; i++) {
        for (int j = i + 1; j < t->n_configs; j++) {
            for (int g = 0; g < t->games_per_pair; g++) {
                /* Alternate colors */
                int white_idx = (g % 2 == 0) ? i : j;
                int black_idx = (g % 2 == 0) ? j : i;

                MCTSConfig w_cfg = mcts_default_config();
                w_cfg.policy     = t->configs[white_idx].policy;
                w_cfg.cp         = t->configs[white_idx].cp;
                w_cfg.cpuct      = t->configs[white_idx].cpuct;
                w_cfg.time_limit = t->time_limit;

                MCTSConfig b_cfg = mcts_default_config();
                b_cfg.policy     = t->configs[black_idx].policy;
                b_cfg.cp         = t->configs[black_idx].cp;
                b_cfg.cpuct      = t->configs[black_idx].cpuct;
                b_cfg.time_limit = t->time_limit;

                SelfPlayResult res = selfplay_game(&w_cfg, &b_cfg, t->verbose);
                total_matches++;

                t->configs[white_idx].games++;
                t->configs[black_idx].games++;

                if (res.result == RESULT_WHITE) {
                    t->configs[white_idx].wins++;
                    t->configs[black_idx].losses++;
                } else if (res.result == RESULT_BLACK) {
                    t->configs[black_idx].wins++;
                    t->configs[white_idx].losses++;
                } else {
                    t->configs[white_idx].draws++;
                    t->configs[black_idx].draws++;
                }

                printf("  Match %d: Config %d (W) vs Config %d (B) → %s  [%d moves]\n",
                       total_matches,
                       t->configs[white_idx].id, t->configs[black_idx].id,
                       res.result == RESULT_WHITE ? "White" :
                       res.result == RESULT_BLACK ? "Black" : "Draw",
                       res.total_moves);
            }
        }
    }

    /* Compute scores */
    for (int i = 0; i < t->n_configs; i++) {
        t->configs[i].score = t->configs[i].wins + 0.5 * t->configs[i].draws;
    }

    printf("\n");
    tournament_print_results(t);
}

void tournament_print_results(const Tournament *t) {
    printf("%-6s %-8s %-8s %-8s %-6s %-6s %-6s %-8s\n",
           "ID", "Policy", "Cp/Cpuct", "Score", "W", "L", "D", "Games");
    printf("----------------------------------------------------------\n");

    for (int i = 0; i < t->n_configs; i++) {
        const TuningConfig *c = &t->configs[i];
        double param = (c->policy == POLICY_UCB1) ? c->cp : c->cpuct;
        printf("%-6d %-8s %-8.3f %-6.1f %-6d %-6d %-6d %-8d\n",
               c->id,
               c->policy == POLICY_UCB1 ? "UCB1" : "PUCT",
               param,
               c->score,
               c->wins, c->losses, c->draws, c->games);
    }
    printf("\n");
}

/* ------------------------------------------------------------------ */
/*  Genetic Algorithm                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    double param;
    double fitness;
} Individual;

static double evaluate_fitness(double param, const GeneticConfig *cfg) {
    MCTSConfig test_cfg = mcts_default_config();
    test_cfg.policy = cfg->policy;
    test_cfg.time_limit = cfg->time_limit;
    if (cfg->policy == POLICY_UCB1)
        test_cfg.cp = param;
    else
        test_cfg.cpuct = param;

    /* Play against a baseline with default parameters */
    MCTSConfig baseline = mcts_default_config();
    baseline.policy = cfg->policy;
    baseline.time_limit = cfg->time_limit;

    int wins = 0, draws = 0;
    for (int g = 0; g < cfg->games_per_eval; g++) {
        MCTSConfig *w, *b;
        bool test_is_white = (g % 2 == 0);

        if (test_is_white) { w = &test_cfg; b = &baseline; }
        else               { w = &baseline; b = &test_cfg; }

        SelfPlayResult res = selfplay_game(w, b, false);

        if (test_is_white) {
            if (res.result == RESULT_WHITE) wins++;
            else if (res.result == RESULT_DRAW) draws++;
        } else {
            if (res.result == RESULT_BLACK) wins++;
            else if (res.result == RESULT_DRAW) draws++;
        }
    }

    return (double)wins + 0.5 * (double)draws;
}

double genetic_tune(const GeneticConfig *cfg) {
    int pop_size = cfg->population_size;
    Individual *pop = (Individual *)malloc(pop_size * sizeof(Individual));

    printf("========================================\n");
    printf(" Genetic Algorithm (%s, pop=%d, gens=%d)\n",
           cfg->policy == POLICY_UCB1 ? "UCB1" : "PUCT",
           pop_size, cfg->generations);
    printf("========================================\n\n");

    /* Initialise population */
    for (int i = 0; i < pop_size; i++) {
        pop[i].param = cfg->param_min + rand_double() * (cfg->param_max - cfg->param_min);
        pop[i].fitness = 0.0;
    }

    double best_overall_param = (cfg->param_min + cfg->param_max) / 2.0;
    double best_overall_fitness = -1.0;

    for (int gen = 0; gen < cfg->generations; gen++) {
        /* Evaluate fitness */
        for (int i = 0; i < pop_size; i++) {
            pop[i].fitness = evaluate_fitness(pop[i].param, cfg);
            if (cfg->verbose) {
                printf("  Gen %d, Ind %d: param=%.4f fitness=%.1f\n",
                       gen, i, pop[i].param, pop[i].fitness);
            }
        }

        /* Find best */
        int best_idx = 0;
        for (int i = 1; i < pop_size; i++) {
            if (pop[i].fitness > pop[best_idx].fitness)
                best_idx = i;
        }

        printf("  Gen %d best: param=%.4f fitness=%.1f\n",
               gen, pop[best_idx].param, pop[best_idx].fitness);

        if (pop[best_idx].fitness > best_overall_fitness) {
            best_overall_fitness = pop[best_idx].fitness;
            best_overall_param = pop[best_idx].param;
        }

        /* Selection + crossover + mutation → next generation */
        Individual *new_pop = (Individual *)malloc(pop_size * sizeof(Individual));

        /* Elitism: keep best */
        new_pop[0] = pop[best_idx];

        for (int i = 1; i < pop_size; i++) {
            /* Tournament selection (size 3) */
            int a = (int)(rand_double() * pop_size) % pop_size;
            int b = (int)(rand_double() * pop_size) % pop_size;
            int c = (int)(rand_double() * pop_size) % pop_size;
            int parent1 = (pop[a].fitness >= pop[b].fitness) ? a : b;
            parent1 = (pop[parent1].fitness >= pop[c].fitness) ? parent1 : c;

            a = (int)(rand_double() * pop_size) % pop_size;
            b = (int)(rand_double() * pop_size) % pop_size;
            c = (int)(rand_double() * pop_size) % pop_size;
            int parent2 = (pop[a].fitness >= pop[b].fitness) ? a : b;
            parent2 = (pop[parent2].fitness >= pop[c].fitness) ? parent2 : c;

            /* Crossover (blend) */
            double child_param;
            if (rand_double() < cfg->crossover_rate) {
                double alpha = rand_double();
                child_param = alpha * pop[parent1].param + (1.0 - alpha) * pop[parent2].param;
            } else {
                child_param = pop[parent1].param;
            }

            /* Mutation (Gaussian) */
            if (rand_double() < cfg->mutation_rate) {
                double range = cfg->param_max - cfg->param_min;
                /* Box-Muller approximation */
                double u1 = rand_double() + 1e-10;
                double u2 = rand_double();
                double z = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265 * u2);
                child_param += z * range * 0.1;
            }

            /* Clamp */
            if (child_param < cfg->param_min) child_param = cfg->param_min;
            if (child_param > cfg->param_max) child_param = cfg->param_max;

            new_pop[i].param = child_param;
            new_pop[i].fitness = 0.0;
        }

        memcpy(pop, new_pop, pop_size * sizeof(Individual));
        free(new_pop);
    }

    printf("\n  Best parameter found: %.4f (fitness=%.1f)\n\n",
           best_overall_param, best_overall_fitness);

    free(pop);
    return best_overall_param;
}
