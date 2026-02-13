/*
 * gui.h - High-level GUI controller (renderer-agnostic)
 *
 * Orchestrates the game loop using a Renderer interface.
 * All game logic is independent of the rendering backend.
 */

#ifndef GUI_H
#define GUI_H

#include "types.h"
#include "renderer.h"
#include "game.h"
#include "mcts.h"

/* ------------------------------------------------------------------ */
/*  GUI application state                                              */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Game state */
    GameState       state;
    HashHistory     history;
    GameResult      result;

    /* AI */
    MCTSSearch      search;
    MCTSConfig      ai_config;

    /* Settings */
    PolicyType      policy;
    int             time_idx;      /* index into TIME_BUDGETS[]        */
    Color           human_side;    /* which side the human plays       */

    /* Interaction state */
    int             selected_sq;   /* currently selected square (-1)   */
    Move            legal_moves[MAX_MOVES];
    int             n_legal;
    int             last_from;     /* highlight last move              */
    int             last_to;

    /* Renderer (abstraction) */
    Renderer       *renderer;

    /* Flags */
    bool            running;
    bool            ai_thinking;
} GUIApp;

/* ------------------------------------------------------------------ */
/*  API                                                                */
/* ------------------------------------------------------------------ */

/* Initialise GUI application with a renderer backend */
bool gui_init(GUIApp *app, Renderer *renderer);

/* Run the main game loop (blocks until quit) */
void gui_run(GUIApp *app);

/* Cleanup */
void gui_destroy(GUIApp *app);

#endif /* GUI_H */
