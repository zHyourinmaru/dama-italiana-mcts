/*
 * gui.c - Renderer-agnostic game loop controller
 *
 * All game logic runs through the abstract Renderer interface.
 * No SDL2 or any other graphics-library code lives here.
 */

#include "gui.h"
#include "bitboard.h"
#include "movegen.h"
#include "game.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/*  Helpers                                                            */
/* ------------------------------------------------------------------ */

static void gui_new_game(GUIApp *app) {
    app->state = game_initial_state();
    hash_history_init(&app->history);
    hash_history_push(&app->history, app->state.hash);
    app->result = RESULT_ONGOING;
    app->selected_sq = -1;
    app->n_legal = generate_moves(&app->state, app->legal_moves);
    app->last_from = -1;
    app->last_to = -1;
    app->ai_thinking = false;
    app->ai_last_stats[0] = '\0';
}

static void gui_update_ai_config(GUIApp *app) {
    app->ai_config.policy = app->policy;
    app->ai_config.time_limit = TIME_BUDGETS[app->time_idx];
    /* Reconfigure search */
    mcts_destroy(&app->search);
    mcts_init(&app->search, &app->ai_config);
}

static void gui_apply_move(GUIApp *app, const Move *m) {
    MoveBackup backup;
    game_make_move(&app->state, m, &backup);
    hash_history_push(&app->history, app->state.hash);

    app->last_from = m->from;
    app->last_to = m->to;
    app->selected_sq = -1;

    /* Check for repetition draw */
    if (hash_history_count(&app->history, app->state.hash) >= 3) {
        app->result = RESULT_DRAW;
    } else {
        app->result = game_result(&app->state);
    }

    if (app->result == RESULT_ONGOING) {
        app->n_legal = generate_moves(&app->state, app->legal_moves);
    } else {
        app->n_legal = 0;
    }
}

static const char *result_text(GameResult r) {
    switch (r) {
        case RESULT_WHITE: return "White wins!";
        case RESULT_BLACK: return "Black wins!";
        case RESULT_DRAW:  return "Draw!";
        default:           return "";
    }
}

/* Render the full scene */
static void gui_render(GUIApp *app) {
    Renderer *r = app->renderer;

    r->begin_frame(r);
    r->draw_menu(r, app->policy, app->time_idx, app->human_side);
    r->draw_board(r);

    /* Draw highlights */
    if (app->last_from >= 0) {
        r->draw_highlight(r, sq_to_row(app->last_from),
                          sq_to_col(app->last_from), 2);
        r->draw_highlight(r, sq_to_row(app->last_to),
                          sq_to_col(app->last_to), 2);
    }

    if (app->selected_sq >= 0) {
        r->draw_highlight(r, sq_to_row(app->selected_sq),
                          sq_to_col(app->selected_sq), 0);
        /* Show legal destinations for selected piece */
        for (int i = 0; i < app->n_legal; i++) {
            if (app->legal_moves[i].from == app->selected_sq) {
                int to = app->legal_moves[i].to;
                r->draw_highlight(r, sq_to_row(to), sq_to_col(to), 1);
            }
        }
    }

    /* Draw pieces */
    for (int sq = 0; sq < NUM_SQUARES; sq++) {
        Bitboard bit = (Bitboard)1u << sq;
        bool is_king = (app->state.k & bit) != 0;

        if (app->state.wp & bit) {
            r->draw_piece(r, sq_to_row(sq), sq_to_col(sq), WHITE, is_king);
        } else if (app->state.bp & bit) {
            r->draw_piece(r, sq_to_row(sq), sq_to_col(sq), BLACK, is_king);
        }
    }

    /* Status text */
    char status[128];
    if (app->result != RESULT_ONGOING) {
        snprintf(status, sizeof(status), "%s  (Click New Game to restart)",
                 result_text(app->result));
    } else if (app->ai_thinking) {
        snprintf(status, sizeof(status), "AI is thinking...");
    } else {
        const char *turn = (app->state.turn == WHITE) ? "White" : "Black";
        bool is_human = (app->state.turn == app->human_side);
        if (is_human && app->ai_last_stats[0] != '\0') {
            snprintf(status, sizeof(status), "%s's turn (your move) | %s", turn, app->ai_last_stats);
        } else {
            snprintf(status, sizeof(status), "%s's turn %s",
                     turn, is_human ? "(your move)" : "(AI)");
        }
    }
    r->draw_status(r, status);
    r->draw_thinking(r, app->ai_thinking);

    r->end_frame(r);
}

/* ------------------------------------------------------------------ */
/*  Handle human click on board                                        */
/* ------------------------------------------------------------------ */

static void gui_handle_click(GUIApp *app, int row, int col) {
    if (app->result != RESULT_ONGOING) return;
    if (app->state.turn != app->human_side) return;

    /* Check if this is a dark square */
    int sq = rowcol_to_sq(row, col);
    if (sq < 0) return;

    /* If we have a selected piece, check if clicking on a legal destination */
    if (app->selected_sq >= 0) {
        for (int i = 0; i < app->n_legal; i++) {
            if (app->legal_moves[i].from == app->selected_sq &&
                app->legal_moves[i].to == sq) {
                /* Apply the move */
                gui_apply_move(app, &app->legal_moves[i]);
                return;
            }
        }
    }

    /* Check if clicking on own piece */
    Bitboard bit = (Bitboard)1u << sq;
    Bitboard own = (app->human_side == WHITE) ? app->state.wp : app->state.bp;

    if (own & bit) {
        /* Check if this piece has any legal moves */
        bool has_move = false;
        for (int i = 0; i < app->n_legal; i++) {
            if (app->legal_moves[i].from == sq) {
                has_move = true;
                break;
            }
        }
        if (has_move) {
            app->selected_sq = sq;
        } else {
            app->selected_sq = -1;
        }
    } else {
        app->selected_sq = -1;
    }
}

/* ------------------------------------------------------------------ */
/*  AI turn                                                            */
/* ------------------------------------------------------------------ */

static void gui_ai_turn(GUIApp *app) {
    if (app->result != RESULT_ONGOING) return;
    if (app->state.turn == app->human_side) return;

    app->ai_thinking = true;
    gui_render(app);  /* Show "thinking" state before blocking */

    Move best = mcts_search(&app->search, &app->state, &app->history);
    
    MCTSStats stats = mcts_get_stats(&app->search);
    snprintf(app->ai_last_stats, sizeof(app->ai_last_stats),
             "AI: %u sims | winrate %.1f%% | %.1fs",
             (unsigned int)stats.simulations,
             (double)stats.best_winrate * 100.0,
             stats.elapsed);
             
    app->ai_thinking = false;

    gui_apply_move(app, &best);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

bool gui_init(GUIApp *app, Renderer *renderer) {
    memset(app, 0, sizeof(GUIApp));
    app->renderer = renderer;
    app->policy = POLICY_UCB1;
    app->time_idx = 1;  /* 1.0s default */
    app->human_side = WHITE;
    app->running = true;
    app->selected_sq = -1;

    /* Init renderer */
    if (!renderer->init(renderer, 0, 0, "Dama Italiana - MCTS AI")) {
        return false;
    }

    /* Init AI */
    app->ai_config = mcts_default_config();
    app->ai_config.policy = app->policy;
    app->ai_config.time_limit = TIME_BUDGETS[app->time_idx];
    if (!mcts_init(&app->search, &app->ai_config)) {
        renderer->destroy(renderer);
        return false;
    }

    gui_new_game(app);
    return true;
}

void gui_run(GUIApp *app) {
    while (app->running) {
        /* Process input events */
        InputEvent ev;
        while ((ev = app->renderer->poll_event(app->renderer)).type != INPUT_NONE) {
            switch (ev.type) {
            case INPUT_QUIT:
                app->running = false;
                break;

            case INPUT_CLICK:
                gui_handle_click(app, ev.board_row, ev.board_col);
                break;

            case INPUT_MENU_POLICY:
                app->policy = (PolicyType)ev.value;
                gui_update_ai_config(app);
                break;

            case INPUT_MENU_TIME:
                app->time_idx = ev.value;
                gui_update_ai_config(app);
                break;

            case INPUT_MENU_SIDE:
                app->human_side = OPPONENT(app->human_side);
                gui_new_game(app);
                gui_update_ai_config(app);
                break;

            case INPUT_MENU_NEWGAME:
                gui_new_game(app);
                break;

            case INPUT_KEY:
                /* ESC to quit */
                if (ev.key == 27) app->running = false;
                break;

            default:
                break;
            }
        }

        /* AI turn */
        if (app->result == RESULT_ONGOING &&
            app->state.turn != app->human_side) {
            gui_ai_turn(app);
        }

        gui_render(app);
        app->renderer->delay(app->renderer, 16); /* ~60 FPS */
    }
}

void gui_destroy(GUIApp *app) {
    mcts_destroy(&app->search);
    if (app->renderer) {
        app->renderer->destroy(app->renderer);
    }
}
