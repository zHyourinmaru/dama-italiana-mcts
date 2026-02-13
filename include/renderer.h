/*
 * renderer.h - Abstract rendering interface
 *
 * Complete separation of presentation from logic.
 * Any graphics backend (SDL2, OpenGL, terminal, etc.) can be swapped in
 * by providing an implementation of these function pointers.
 */

#ifndef RENDERER_H
#define RENDERER_H

#include "types.h"
#include <stdbool.h>

/* ------------------------------------------------------------------ */
/*  Input event abstraction                                            */
/* ------------------------------------------------------------------ */

typedef enum {
    INPUT_NONE = 0,
    INPUT_QUIT,
    INPUT_CLICK,           /* mouse/touch click on board              */
    INPUT_KEY,             /* keyboard input                          */
    INPUT_MENU_POLICY,     /* user toggled policy UCB1/PUCT           */
    INPUT_MENU_TIME,       /* user changed time budget                */
    INPUT_MENU_NEWGAME,    /* user requested new game                 */
    INPUT_MENU_SIDE,       /* user toggled playing side               */
} InputType;

typedef struct {
    InputType type;
    int       board_row;   /* for INPUT_CLICK: row 0-7                */
    int       board_col;   /* for INPUT_CLICK: col 0-7                */
    int       key;         /* for INPUT_KEY: keycode                  */
    int       value;       /* generic integer payload                 */
} InputEvent;

/* ------------------------------------------------------------------ */
/*  Renderer interface (vtable)                                        */
/* ------------------------------------------------------------------ */

typedef struct Renderer Renderer;

struct Renderer {
    /* Lifecycle */
    bool (*init)(Renderer *self, int width, int height, const char *title);
    void (*destroy)(Renderer *self);

    /* Drawing */
    void (*begin_frame)(Renderer *self);
    void (*end_frame)(Renderer *self);

    /* Board & pieces */
    void (*draw_board)(Renderer *self);
    void (*draw_piece)(Renderer *self, int row, int col, Color color, bool is_king);
    void (*draw_highlight)(Renderer *self, int row, int col, int highlight_type);
    /* highlight_type: 0=selected, 1=legal move, 2=last move */

    /* UI text */
    void (*draw_status)(Renderer *self, const char *text);
    void (*draw_menu)(Renderer *self, PolicyType policy, int time_idx, Color human_side);
    void (*draw_thinking)(Renderer *self, bool is_thinking);

    /* Input polling */
    InputEvent (*poll_event)(Renderer *self);

    /* Timing */
    void (*delay)(Renderer *self, int ms);

    /* Backend-specific data (opaque) */
    void *backend_data;
};

/* ------------------------------------------------------------------ */
/*  Factory functions for available backends                           */
/* ------------------------------------------------------------------ */

/* SDL2 backend */
Renderer *renderer_sdl2_create(void);

/* Future: add other backends here
 * Renderer *renderer_opengl_create(void);
 * Renderer *renderer_terminal_create(void);
 */

#endif /* RENDERER_H */
