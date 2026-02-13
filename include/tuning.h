/*
 * tuning.h - Hyperparameter tuning framework
 *
 * Provides round-robin self-play tournaments and a genetic algorithm
 * for tuning MCTS exploration constants (Cp for UCB1, Cpuct for PUCT).
 */

#ifndef TUNING_H
#define TUNING_H

#include "types.h"
#include "mcts.h"

/* ------------------------------------------------------------------ */
/*  Configuration for a single player in a tournament                  */
/* ------------------------------------------------------------------ */

typedef struct {
    int         id;
    PolicyType  policy;
    double      cp;         /* UCB1 exploration constant              */
    double      cpuct;      /* PUCT exploration constant              */
    double      time_limit;
    /* Results */
    int         wins;
    int         losses;
    int         draws;
    int         games;
    double      avg_sims;   /* average simulations per move           */
    double      score;      /* fitness: wins + 0.5*draws              */
} TuningConfig;

/* ------------------------------------------------------------------ */
/*  Tournament                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    TuningConfig *configs;
    int           n_configs;
    int           games_per_pair; /* games per matchup                 */
    double        time_limit;    /* override time for all players     */
    bool          verbose;
} Tournament;

/* Run a round-robin tournament; prints results to stdout */
void tournament_run(Tournament *t);

/* Print results table */
void tournament_print_results(const Tournament *t);

/* ------------------------------------------------------------------ */
/*  Genetic Algorithm                                                  */
/* ------------------------------------------------------------------ */

typedef struct {
    int         population_size;
    int         generations;
    int         games_per_eval;  /* games per fitness evaluation      */
    double      time_limit;
    double      mutation_rate;
    double      crossover_rate;
    PolicyType  policy;          /* which policy to tune              */
    double      param_min;       /* min value for Cp/Cpuct            */
    double      param_max;       /* max value                         */
    bool        verbose;
} GeneticConfig;

/* Run genetic algorithm; returns best parameter value */
double genetic_tune(const GeneticConfig *cfg);

/* ------------------------------------------------------------------ */
/*  Self-play game (utility)                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    GameResult result;
    uint64_t   white_sims;
    uint64_t   black_sims;
    int        total_moves;
} SelfPlayResult;

SelfPlayResult selfplay_game(const MCTSConfig *white_cfg,
                             const MCTSConfig *black_cfg,
                             bool verbose);

#endif /* TUNING_H */
