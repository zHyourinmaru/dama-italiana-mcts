/*
 * main.c - Entry point for Dama Italiana MCTS AI
 *
 * Modes:
 *   --mode gui       Interactive GUI (default)
 *   --mode selfplay   AI vs AI single game
 *   --mode tuning     Hyperparameter tuning
 *   --mode test       Built-in correctness tests
 *
 * Options:
 *   --policy ucb1|puct     Selection policy (default: ucb1)
 *   --time 0.2|1.0|3.0     Time budget in seconds (default: 1.0)
 *   --cp <float>           UCB1 exploration constant (default: sqrt(2))
 *   --cpuct <float>        PUCT exploration constant (default: 1.0)
 *   --games <int>          Games for selfplay/tuning (default: 10)
 *   --population <int>     GA population size (default: 8)
 *   --generations <int>    GA generations (default: 5)
 *   --verbose              Verbose output
 */

#include "types.h"
#include "bitboard.h"
#include "movegen.h"
#include "game.h"
#include "mcts.h"
#include "renderer.h"
#include "gui.h"
#include "tuning.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Argument parsing                                                   */
/* ------------------------------------------------------------------ */

typedef enum {
    MODE_GUI,
    MODE_SELFPLAY,
    MODE_TUNING,
    MODE_TEST
} RunMode;

typedef struct {
    RunMode     mode;
    PolicyType  policy;
    double      time_limit;
    double      cp;
    double      cpuct;
    int         games;
    int         population;
    int         generations;
    bool        verbose;
    char       *output_file;
} AppArgs;

static AppArgs parse_args(int argc, char **argv) {
    AppArgs args;
    args.mode        = MODE_GUI;
    args.policy      = POLICY_UCB1;
    args.time_limit  = 1.0;
    args.cp          = 1.41421356;
    args.cpuct       = 1.0;
    args.games       = 10;
    args.population  = 8;
    args.generations = 5;
    args.verbose     = false;
    args.output_file = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if      (strcmp(argv[i], "gui") == 0)      args.mode = MODE_GUI;
            else if (strcmp(argv[i], "selfplay") == 0)  args.mode = MODE_SELFPLAY;
            else if (strcmp(argv[i], "tuning") == 0)    args.mode = MODE_TUNING;
            else if (strcmp(argv[i], "test") == 0)      args.mode = MODE_TEST;
        }
        else if (strcmp(argv[i], "--policy") == 0 && i + 1 < argc) {
            i++;
            if      (strcmp(argv[i], "ucb1") == 0) args.policy = POLICY_UCB1;
            else if (strcmp(argv[i], "puct") == 0) args.policy = POLICY_PUCT;
        }
        else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            args.time_limit = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--cp") == 0 && i + 1 < argc) {
            args.cp = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--cpuct") == 0 && i + 1 < argc) {
            args.cpuct = atof(argv[++i]);
        }
        else if (strcmp(argv[i], "--games") == 0 && i + 1 < argc) {
            args.games = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--population") == 0 && i + 1 < argc) {
            args.population = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--generations") == 0 && i + 1 < argc) {
            args.generations = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--verbose") == 0) {
            args.verbose = true;
        }
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) {
            args.output_file = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0) {
            printf("Dama Italiana - MCTS AI Player\n\n");
            printf("Usage: dama_italiana [options]\n\n");
            printf("  --mode gui|selfplay|tuning|test   Run mode (default: gui)\n");
            printf("  --policy ucb1|puct                Selection policy (default: ucb1)\n");
            printf("  --time <seconds>                  Time budget (default: 1.0)\n");
            printf("  --cp <float>                      UCB1 Cp (default: 1.414)\n");
            printf("  --cpuct <float>                   PUCT Cpuct (default: 1.0)\n");
            printf("  --games <int>                     Games for selfplay/tuning (default: 10)\n");
            printf("  --population <int>                GA population (default: 8)\n");
            printf("  --generations <int>               GA generations (default: 5)\n");
            printf("  --verbose                         Verbose output\n");
            printf("  --output <file.csv>               Export results to CSV\n");
            exit(0);
        }
    }

    return args;
}

/* ------------------------------------------------------------------ */
/*  GUI mode                                                           */
/* ------------------------------------------------------------------ */

static int run_gui(const AppArgs *args) {
    Renderer *renderer = renderer_sdl2_create();
    GUIApp app;

    if (!gui_init(&app, renderer)) {
        fprintf(stderr, "Failed to initialise GUI\n");
        return 1;
    }

    /* Apply args */
    app.policy = args->policy;
    app.ai_config.policy = args->policy;
    app.ai_config.cp     = args->cp;
    app.ai_config.cpuct  = args->cpuct;
    app.ai_config.time_limit = args->time_limit;

    /* Find time index */
    for (int i = 0; i < NUM_TIME_BUDGETS; i++) {
        if (TIME_BUDGETS[i] >= args->time_limit - 0.01) {
            app.time_idx = i;
            break;
        }
    }

    /* Re-init MCTS with updated config */
    mcts_destroy(&app.search);
    mcts_init(&app.search, &app.ai_config);

    gui_run(&app);
    gui_destroy(&app);

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Self-play mode                                                     */
/* ------------------------------------------------------------------ */

static int run_selfplay(const AppArgs *args) {
    MCTSConfig white_cfg = mcts_default_config();
    white_cfg.policy = args->policy;
    white_cfg.cp     = args->cp;
    white_cfg.cpuct  = args->cpuct;
    white_cfg.time_limit = args->time_limit;

    MCTSConfig black_cfg = white_cfg; /* mirror config */

    printf("Self-play: %d games, policy=%s, time=%.1fs, Cp=%.3f, Cpuct=%.3f\n\n",
           args->games,
           args->policy == POLICY_UCB1 ? "UCB1" : "PUCT",
           args->time_limit, args->cp, args->cpuct);

    int w_wins = 0, b_wins = 0, draws = 0;

    for (int g = 0; g < args->games; g++) {
        printf("Game %d/%d... ", g + 1, args->games);
        fflush(stdout);

        SelfPlayResult res = selfplay_game(&white_cfg, &black_cfg, args->verbose);

        const char *result_str;
        switch (res.result) {
            case RESULT_WHITE: result_str = "White"; w_wins++; break;
            case RESULT_BLACK: result_str = "Black"; b_wins++; break;
            default:           result_str = "Draw";  draws++;  break;
        }

        printf("%s wins (%d moves, W:%llu sims, B:%llu sims)\n",
               result_str, res.total_moves,
               (unsigned long long)res.white_sims,
               (unsigned long long)res.black_sims);
               
        if (args->output_file) {
            FILE *f = fopen(args->output_file, g == 0 ? "w" : "a");
            if (f) {
                if (g == 0) {
                    fprintf(f, "game,result,total_moves,white_sims,black_sims,sims_per_sec\n");
                }
                double sps = (res.white_sims + res.black_sims) / (args->time_limit * 2.0 * (res.total_moves/2.0+1));
                fprintf(f, "%d,%s,%d,%llu,%llu,%.1f\n",
                        g+1, result_str, res.total_moves, 
                        (unsigned long long)res.white_sims, (unsigned long long)res.black_sims, sps);
                fclose(f);
            }
        }
    }

    printf("\nResults: White=%d Black=%d Draw=%d\n", w_wins, b_wins, draws);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Tuning mode                                                        */
/* ------------------------------------------------------------------ */

static int run_tuning(const AppArgs *args) {
    printf("=== Hyperparameter Tuning ===\n\n");

    /* Phase 1: Round-robin with different Cp values */
    printf("--- Phase 1: Round-Robin Tournament ---\n\n");
    TuningConfig configs[] = {
        { 1, POLICY_UCB1, 0.5,  0.0, 0.2, 0,0,0,0,0,0 },
        { 2, POLICY_UCB1, 1.0,  0.0, 0.2, 0,0,0,0,0,0 },
        { 3, POLICY_UCB1, 1.414,0.0, 0.2, 0,0,0,0,0,0 },
        { 4, POLICY_UCB1, 2.0,  0.0, 0.2, 0,0,0,0,0,0 },
        { 5, POLICY_PUCT, 0.0,  0.5, 0.2, 0,0,0,0,0,0 },
        { 6, POLICY_PUCT, 0.0,  1.0, 0.2, 0,0,0,0,0,0 },
        { 7, POLICY_PUCT, 0.0,  1.5, 0.2, 0,0,0,0,0,0 },
        { 8, POLICY_PUCT, 0.0,  2.5, 0.2, 0,0,0,0,0,0 },
    };

    Tournament t;
    t.configs = configs;
    t.n_configs = 8;
    t.games_per_pair = args->games > 0 ? (args->games < 4 ? 2 : args->games / 4) : 2;
    t.time_limit = 0.2;  /* fast tuning */
    t.verbose = args->verbose;

    tournament_run(&t);

    /* Phase 2: GA fine-tuning for UCB1 */
    printf("--- Phase 2: Genetic Algorithm (UCB1 Cp) ---\n\n");
    GeneticConfig ga_ucb1;
    ga_ucb1.population_size = args->population;
    ga_ucb1.generations     = args->generations;
    ga_ucb1.games_per_eval  = 4;
    ga_ucb1.time_limit      = 0.2;
    ga_ucb1.mutation_rate   = 0.3;
    ga_ucb1.crossover_rate  = 0.7;
    ga_ucb1.policy          = POLICY_UCB1;
    ga_ucb1.param_min       = 0.1;
    ga_ucb1.param_max       = 3.0;
    ga_ucb1.verbose         = args->verbose;

    double best_cp = genetic_tune(&ga_ucb1);
    printf("Best UCB1 Cp: %.4f\n\n", best_cp);

    /* Phase 3: GA fine-tuning for PUCT */
    printf("--- Phase 3: Genetic Algorithm (PUCT Cpuct) ---\n\n");
    GeneticConfig ga_puct = ga_ucb1;
    ga_puct.policy = POLICY_PUCT;

    double best_cpuct = genetic_tune(&ga_puct);
    printf("Best PUCT Cpuct: %.4f\n\n", best_cpuct);

    /* Phase 4: Sequential Halving (BAI) — principled best-arm identification */
    printf("--- Phase 4: Sequential Halving BAI (UCB1 Cp) ---\n\n");
    SeqHalvConfig sh_ucb1;
    sh_ucb1.policy        = POLICY_UCB1;
    sh_ucb1.param_min     = 0.1;
    sh_ucb1.param_max     = 3.0;
    sh_ucb1.n_candidates  = 8;   /* 3 rounds: 8→4→2→1 */
    sh_ucb1.total_budget  = (args->games > 0) ? args->games * 4 : 32;
    sh_ucb1.time_limit    = 0.2;
    sh_ucb1.verbose       = args->verbose;

    double sha_cp = seq_halving_tune(&sh_ucb1);
    printf("Sequential Halving best UCB1 Cp: %.4f\n\n", sha_cp);

    printf("--- Phase 4b: Sequential Halving BAI (PUCT Cpuct) ---\n\n");
    SeqHalvConfig sh_puct = sh_ucb1;
    sh_puct.policy = POLICY_PUCT;

    double sha_cpuct = seq_halving_tune(&sh_puct);
    printf("Sequential Halving best PUCT Cpuct: %.4f\n\n", sha_cpuct);

    /* Final summary */
    printf("========================================\n");
    printf("  TUNING RESULTS\n");
    printf("  GA   UCB1 Cp:         %.4f\n", best_cp);
    printf("  GA   PUCT Cpuct:      %.4f\n", best_cpuct);
    printf("  SHA  UCB1 Cp:         %.4f\n", sha_cp);
    printf("  SHA  PUCT Cpuct:      %.4f\n", sha_cpuct);
    printf("========================================\n");

    if (args->output_file) {
        FILE *f = fopen(args->output_file, "w");
        if (f) {
            fprintf(f, "algorithm,policy,best_param\n");
            fprintf(f, "GA,UCB1,%.4f\n", best_cp);
            fprintf(f, "GA,PUCT,%.4f\n", best_cpuct);
            fprintf(f, "Sequential_Halving,UCB1,%.4f\n", sha_cp);
            fprintf(f, "Sequential_Halving,PUCT,%.4f\n", sha_cpuct);
            fclose(f);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/*  Test mode                                                          */
/* ------------------------------------------------------------------ */

static int run_tests(void) {
    int passed = 0, failed = 0;

    printf("=== Built-in Tests ===\n\n");

    /* Test 1: Initial position legal moves */
    {
        GameState s = game_initial_state();
        Move moves[MAX_MOVES];
        int n = generate_moves(&s, moves);
        printf("Test 1 - Initial position moves: %d ... ", n);
        if (n == 7) { printf("PASS\n"); passed++; }
        else        { printf("FAIL (expected 7)\n"); failed++; }
    }

    /* Test 2: Piece counts at start */
    {
        GameState s = game_initial_state();
        int wp = popcount32(s.wp);
        int bp = popcount32(s.bp);
        printf("Test 2 - Starting pieces: W=%d B=%d ... ", wp, bp);
        if (wp == 12 && bp == 12) { printf("PASS\n"); passed++; }
        else                      { printf("FAIL\n"); failed++; }
    }

    /* Test 3: No kings at start */
    {
        GameState s = game_initial_state();
        printf("Test 3 - No kings at start: %d ... ", popcount32(s.k));
        if (s.k == 0) { printf("PASS\n"); passed++; }
        else          { printf("FAIL\n"); failed++; }
    }

    /* Test 4: Make/unmake preserves state */
    {
        GameState s = game_initial_state();
        GameState backup_state = s;
        Move moves[MAX_MOVES];
        int n = generate_moves(&s, moves);
        MoveBackup backup;
        game_make_move(&s, &moves[0], &backup);
        game_unmake_move(&s, &moves[0], &backup);
        bool ok = (s.wp == backup_state.wp && s.bp == backup_state.bp &&
                   s.k == backup_state.k && s.turn == backup_state.turn);
        printf("Test 4 - Make/unmake roundtrip ... ");
        if (ok) { printf("PASS\n"); passed++; }
        else    { printf("FAIL\n"); failed++; }
    }

    /* Test 5: Game result for empty board */
    {
        GameState s;
        memset(&s, 0, sizeof(s));
        s.turn = WHITE;
        GameResult r = game_result(&s);
        printf("Test 5 - Empty board → Black wins ... ");
        if (r == RESULT_BLACK) { printf("PASS\n"); passed++; }
        else                   { printf("FAIL (got %d)\n", r); failed++; }
    }

    /* Test 6: Material score */
    {
        GameState s = game_initial_state();
        int score = game_material_score(&s);
        printf("Test 6 - Initial material is balanced (%d) ... ", score);
        if (score == 0) { printf("PASS\n"); passed++; }
        else            { printf("FAIL\n"); failed++; }
    }

    /* Test 7: Square mapping consistency */
    {
        bool ok = true;
        for (int sq = 0; sq < NUM_SQUARES; sq++) {
            int r = sq_to_row(sq);
            int c = sq_to_col(sq);
            int sq2 = rowcol_to_sq(r, c);
            if (sq2 != sq) { ok = false; break; }
        }
        printf("Test 7 - Square mapping roundtrip ... ");
        if (ok) { printf("PASS\n"); passed++; }
        else    { printf("FAIL\n"); failed++; }
    }

    /* Test 8: Pool allocator */
    {
        Pool p;
        bool ok = pool_init(&p, 64, 1000);
        void *a = pool_alloc(&p);
        void *b = pool_alloc(&p);
        ok = ok && (a != NULL) && (b != NULL) && (a != b);
        ok = ok && (pool_used(&p) == 2);
        pool_reset(&p);
        ok = ok && (pool_used(&p) == 0);
        pool_destroy(&p);
        printf("Test 8 - Pool allocator ... ");
        if (ok) { printf("PASS\n"); passed++; }
        else    { printf("FAIL\n"); failed++; }
    }

    /* Test 9: MCTS can find a move */
    {
        GameState s = game_initial_state();
        MCTSConfig cfg = mcts_default_config();
        cfg.time_limit = 0.1;
        cfg.pool_capacity = 100000;

        MCTSSearch search;
        mcts_init(&search, &cfg);
        Move m = mcts_search(&search, &s, NULL);
        mcts_destroy(&search);

        printf("Test 9 - MCTS produces a move (%d->%d) ... ", m.from, m.to);
        if (m.from != m.to) { printf("PASS\n"); passed++; }
        else                { printf("FAIL\n"); failed++; }
    }

    /* Test 10: Quick selfplay game completes */
    {
        MCTSConfig cfg = mcts_default_config();
        cfg.time_limit = 0.05;
        cfg.pool_capacity = 50000;

        SelfPlayResult res = selfplay_game(&cfg, &cfg, false);
        printf("Test 10 - Selfplay game completes (%d moves, result=%d) ... ",
               res.total_moves, res.result);
        if (res.total_moves > 0) { printf("PASS\n"); passed++; }
        else                     { printf("FAIL\n"); failed++; }
    }

    printf("\n%d/%d tests passed\n", passed, passed + failed);
    return (failed > 0) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/*  Main                                                               */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    AppArgs args = parse_args(argc, argv);

    /* Initialise bitboard lookup tables */
    bitboard_init();

    switch (args.mode) {
    case MODE_GUI:
        return run_gui(&args);
    case MODE_SELFPLAY:
        return run_selfplay(&args);
    case MODE_TUNING:
        return run_tuning(&args);
    case MODE_TEST:
        return run_tests();
    default:
        fprintf(stderr, "Unknown mode\n");
        return 1;
    }
}
