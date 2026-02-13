/*
 * renderer_sdl2.c - SDL2 backend for the Renderer interface
 *
 * Implements all rendering using SDL2 + SDL2_ttf.
 * Completely self-contained: no game logic lives here.
 */

#include "renderer.h"
#include <SDL.h>
#include <SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                          */
/* ------------------------------------------------------------------ */

#define BOARD_PX         640
#define CELL_PX          (BOARD_PX / BOARD_SIZE)          /* 80 */
#define MENU_HEIGHT      120
#define STATUS_HEIGHT    40
#define WINDOW_W         BOARD_PX
#define WINDOW_H         (BOARD_PX + MENU_HEIGHT + STATUS_HEIGHT)

#define PIECE_RADIUS     (CELL_PX / 2 - 8)
#define KING_INNER       (PIECE_RADIUS - 8)

/* Colours */
static const SDL_Color CLR_DARK_SQ    = { 139, 90, 43, 255 };
static const SDL_Color CLR_LIGHT_SQ   = { 238, 213, 183, 255 };
static const SDL_Color CLR_WHITE_PC   = { 255, 250, 240, 255 };
static const SDL_Color CLR_BLACK_PC   = { 40, 40, 40, 255 };
static const SDL_Color CLR_SEL        = { 50, 205, 50, 120 };
static const SDL_Color CLR_LEGAL      = { 100, 180, 255, 120 };
static const SDL_Color CLR_LAST       = { 255, 215, 0, 80 };
static const SDL_Color CLR_KING_MARK  = { 255, 215, 0, 255 };
static const SDL_Color CLR_BG         = { 30, 30, 30, 255 };
static const SDL_Color CLR_TEXT       = { 240, 240, 240, 255 };
static const SDL_Color CLR_BTN_BG     = { 60, 60, 70, 255 };
static const SDL_Color CLR_BTN_HL     = { 80, 130, 200, 255 };
static const SDL_Color CLR_THINKING   = { 255, 100, 100, 255 };

/* ------------------------------------------------------------------ */
/*  Backend data                                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    SDL_Window   *window;
    SDL_Renderer *sdl_renderer;
    TTF_Font     *font;
    TTF_Font     *font_small;
    TTF_Font     *font_large;
    int           width;
    int           height;
} SDL2Backend;

/* ------------------------------------------------------------------ */
/*  Helper: draw filled circle (Midpoint circle algorithm)             */
/* ------------------------------------------------------------------ */

static void draw_filled_circle(SDL_Renderer *r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; dy++) {
        int dx = (int)sqrt((double)(radius * radius - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

/* ------------------------------------------------------------------ */
/*  Helper: draw ring                                                  */
/* ------------------------------------------------------------------ */

static void draw_ring(SDL_Renderer *r, int cx, int cy, int radius, int thickness) {
    for (int t = 0; t < thickness; t++) {
        int rad = radius - t;
        for (int angle = 0; angle < 360; angle++) {
            double a = angle * 3.14159265 / 180.0;
            int x = cx + (int)(rad * cos(a));
            int y = cy + (int)(rad * sin(a));
            SDL_RenderDrawPoint(r, x, y);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  Helper: render text                                                */
/* ------------------------------------------------------------------ */

static void render_text(SDL2Backend *b, const char *text, int x, int y,
                        SDL_Color color, TTF_Font *font, bool center) {
    if (!font || !text || text[0] == '\0') return;
    SDL_Surface *surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) return;
    SDL_Texture *tex = SDL_CreateTextureFromSurface(b->sdl_renderer, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }

    SDL_Rect dst = { x, y, surf->w, surf->h };
    if (center) {
        dst.x = x - surf->w / 2;
        dst.y = y - surf->h / 2;
    }
    SDL_RenderCopy(b->sdl_renderer, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

/* ------------------------------------------------------------------ */
/*  Helper: draw button                                                */
/* ------------------------------------------------------------------ */

static SDL_Rect draw_button(SDL2Backend *b, const char *label, int x, int y,
                            int w, int h, bool active) {
    SDL_Rect rect = { x, y, w, h };
    SDL_Color bg = active ? CLR_BTN_HL : CLR_BTN_BG;
    SDL_SetRenderDrawColor(b->sdl_renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_RenderFillRect(b->sdl_renderer, &rect);

    /* Border */
    SDL_SetRenderDrawColor(b->sdl_renderer, 120, 120, 130, 255);
    SDL_RenderDrawRect(b->sdl_renderer, &rect);

    /* Label */
    render_text(b, label, x + w / 2, y + h / 2, CLR_TEXT, b->font_small, true);
    return rect;
}

/* ------------------------------------------------------------------ */
/*  Interface implementations                                          */
/* ------------------------------------------------------------------ */

static bool sdl2_init(Renderer *self, int width, int height, const char *title) {
    SDL2Backend *b = (SDL2Backend *)calloc(1, sizeof(SDL2Backend));
    if (!b) return false;
    self->backend_data = b;

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[SDL2] Init failed: %s\n", SDL_GetError());
        free(b);
        return false;
    }

    if (TTF_Init() < 0) {
        fprintf(stderr, "[SDL2] TTF Init failed: %s\n", TTF_GetError());
        SDL_Quit();
        free(b);
        return false;
    }

    /* Use provided dimensions or defaults */
    b->width = (width > 0) ? width : WINDOW_W;
    b->height = (height > 0) ? height : WINDOW_H;

    b->window = SDL_CreateWindow(title ? title : "Dama Italiana",
                                  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  b->width, b->height, SDL_WINDOW_SHOWN);
    if (!b->window) {
        fprintf(stderr, "[SDL2] Window creation failed: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        free(b);
        return false;
    }

    b->sdl_renderer = SDL_CreateRenderer(b->window, -1,
                                          SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!b->sdl_renderer) {
        b->sdl_renderer = SDL_CreateRenderer(b->window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!b->sdl_renderer) {
        fprintf(stderr, "[SDL2] Renderer creation failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(b->window);
        TTF_Quit();
        SDL_Quit();
        free(b);
        return false;
    }

    SDL_SetRenderDrawBlendMode(b->sdl_renderer, SDL_BLENDMODE_BLEND);

    /* Load font: try common paths, fall back to bundled */
    const char *font_paths[] = {
        "assets/font.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "C:/Windows/Fonts/segoeui.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        NULL
    };

    for (int i = 0; font_paths[i]; i++) {
        b->font = TTF_OpenFont(font_paths[i], 18);
        if (b->font) {
            b->font_small = TTF_OpenFont(font_paths[i], 14);
            b->font_large = TTF_OpenFont(font_paths[i], 24);
            break;
        }
    }

    if (!b->font) {
        fprintf(stderr, "[SDL2] Warning: No font found, text will not be rendered\n");
    }

    return true;
}

static void sdl2_destroy(Renderer *self) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    if (!b) return;
    if (b->font)       TTF_CloseFont(b->font);
    if (b->font_small) TTF_CloseFont(b->font_small);
    if (b->font_large) TTF_CloseFont(b->font_large);
    if (b->sdl_renderer) SDL_DestroyRenderer(b->sdl_renderer);
    if (b->window)     SDL_DestroyWindow(b->window);
    TTF_Quit();
    SDL_Quit();
    free(b);
    self->backend_data = NULL;
}

static void sdl2_begin_frame(Renderer *self) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    SDL_SetRenderDrawColor(b->sdl_renderer, CLR_BG.r, CLR_BG.g, CLR_BG.b, 255);
    SDL_RenderClear(b->sdl_renderer);
}

static void sdl2_end_frame(Renderer *self) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    SDL_RenderPresent(b->sdl_renderer);
}

static void sdl2_draw_board(Renderer *self) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    int offset_y = MENU_HEIGHT;

    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            /* Display row 7 at top (black's side), row 0 at bottom (white's side) */
            int display_row = 7 - row;
            SDL_Rect cell = {
                col * CELL_PX,
                offset_y + display_row * CELL_PX,
                CELL_PX, CELL_PX
            };

            bool dark = (row + col) % 2 == 1;
            SDL_Color c = dark ? CLR_DARK_SQ : CLR_LIGHT_SQ;
            SDL_SetRenderDrawColor(b->sdl_renderer, c.r, c.g, c.b, c.a);
            SDL_RenderFillRect(b->sdl_renderer, &cell);
        }
    }

    /* Board border */
    SDL_Rect border = { 0, offset_y, BOARD_PX, BOARD_PX };
    SDL_SetRenderDrawColor(b->sdl_renderer, 80, 60, 40, 255);
    SDL_RenderDrawRect(b->sdl_renderer, &border);

    /* Row/col labels */
    if (b->font_small) {
        for (int i = 0; i < BOARD_SIZE; i++) {
            char label[2];

            /* Column labels (a-h) at bottom */
            label[0] = 'a' + i;
            label[1] = '\0';
            render_text(b, label,
                        i * CELL_PX + CELL_PX / 2,
                        offset_y + BOARD_PX + 2,
                        CLR_TEXT, b->font_small, true);

            /* Row labels (1-8) on left */
            label[0] = '1' + i;
            render_text(b, label,
                        -1,
                        offset_y + (7 - i) * CELL_PX + CELL_PX / 2,
                        CLR_TEXT, b->font_small, true);
        }
    }
}

static void sdl2_draw_piece(Renderer *self, int row, int col, Color color, bool is_king) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    int offset_y = MENU_HEIGHT;
    int display_row = 7 - row;

    int cx = col * CELL_PX + CELL_PX / 2;
    int cy = offset_y + display_row * CELL_PX + CELL_PX / 2;

    /* Shadow */
    SDL_SetRenderDrawColor(b->sdl_renderer, 0, 0, 0, 60);
    draw_filled_circle(b->sdl_renderer, cx + 3, cy + 3, PIECE_RADIUS);

    /* Piece body */
    SDL_Color pc = (color == WHITE) ? CLR_WHITE_PC : CLR_BLACK_PC;
    SDL_SetRenderDrawColor(b->sdl_renderer, pc.r, pc.g, pc.b, pc.a);
    draw_filled_circle(b->sdl_renderer, cx, cy, PIECE_RADIUS);

    /* Border ring */
    SDL_Color border = (color == WHITE) ?
        (SDL_Color){200, 190, 170, 255} :
        (SDL_Color){80, 80, 80, 255};
    SDL_SetRenderDrawColor(b->sdl_renderer, border.r, border.g, border.b, border.a);
    draw_ring(b->sdl_renderer, cx, cy, PIECE_RADIUS, 2);

    /* King marker: golden crown circle + "K" */
    if (is_king) {
        SDL_SetRenderDrawColor(b->sdl_renderer, CLR_KING_MARK.r, CLR_KING_MARK.g,
                                CLR_KING_MARK.b, CLR_KING_MARK.a);
        draw_ring(b->sdl_renderer, cx, cy, KING_INNER, 3);

        /* Draw "K" or crown symbol */
        if (b->font_small) {
            SDL_Color kc = (color == WHITE) ?
                (SDL_Color){180, 140, 0, 255} :
                (SDL_Color){255, 215, 0, 255};
            render_text(b, "K", cx, cy, kc, b->font, true);
        }
    }
}

static void sdl2_draw_highlight(Renderer *self, int row, int col, int highlight_type) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    int offset_y = MENU_HEIGHT;
    int display_row = 7 - row;

    SDL_Rect cell = {
        col * CELL_PX,
        offset_y + display_row * CELL_PX,
        CELL_PX, CELL_PX
    };

    SDL_Color c;
    switch (highlight_type) {
        case 0: c = CLR_SEL;   break;  /* selected */
        case 1: c = CLR_LEGAL; break;  /* legal destination */
        case 2: c = CLR_LAST;  break;  /* last move */
        default: return;
    }

    SDL_SetRenderDrawColor(b->sdl_renderer, c.r, c.g, c.b, c.a);
    SDL_RenderFillRect(b->sdl_renderer, &cell);
}

static void sdl2_draw_status(Renderer *self, const char *text) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    int y = MENU_HEIGHT + BOARD_PX + 10;

    /* Status bar background */
    SDL_Rect bar = { 0, MENU_HEIGHT + BOARD_PX, b->width, STATUS_HEIGHT };
    SDL_SetRenderDrawColor(b->sdl_renderer, 40, 40, 50, 255);
    SDL_RenderFillRect(b->sdl_renderer, &bar);

    if (b->font)
        render_text(b, text, b->width / 2, y + STATUS_HEIGHT / 2 - 5, CLR_TEXT, b->font, true);
}

/* ------------------------------------------------------------------ */
/*  Menu rendering: buttons for policy, time, side, new game           */
/* ------------------------------------------------------------------ */

/* Store button rects for hit testing */
static SDL_Rect btn_ucb1, btn_puct;
static SDL_Rect btn_t02, btn_t1, btn_t3;
static SDL_Rect btn_side;
static SDL_Rect btn_newgame;

static void sdl2_draw_menu(Renderer *self, PolicyType policy, int time_idx,
                           Color human_side) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;

    /* Menu background */
    SDL_Rect menu_bg = { 0, 0, b->width, MENU_HEIGHT };
    SDL_SetRenderDrawColor(b->sdl_renderer, 35, 35, 45, 255);
    SDL_RenderFillRect(b->sdl_renderer, &menu_bg);

    int bw = 75, bh = 30, gap = 8;
    int y1 = 10, y2 = 50, y3 = 85;
    int x = 10;

    /* Row 1: Policy selection */
    if (b->font_small) {
        render_text(b, "Policy:", x, y1 + bh / 2, CLR_TEXT, b->font_small, false);
    }
    x = 80;
    btn_ucb1 = draw_button(b, "UCB1", x, y1, bw, bh, policy == POLICY_UCB1);
    x += bw + gap;
    btn_puct = draw_button(b, "PUCT", x, y1, bw, bh, policy == POLICY_PUCT);

    /* Row 2: Time budget */
    x = 10;
    if (b->font_small) {
        render_text(b, "Time:", x, y2 + bh / 2, CLR_TEXT, b->font_small, false);
    }
    x = 80;
    btn_t02 = draw_button(b, "0.2s", x, y2, bw, bh, time_idx == 0);
    x += bw + gap;
    btn_t1 = draw_button(b, "1.0s", x, y2, bw, bh, time_idx == 1);
    x += bw + gap;
    btn_t3 = draw_button(b, "3.0s", x, y2, bw, bh, time_idx == 2);

    /* Row 3: Side + New Game */
    x = 10;
    const char *side_label = (human_side == WHITE) ? "You: White" : "You: Black";
    btn_side = draw_button(b, side_label, x, y3, 100, bh, false);
    x = 120;
    btn_newgame = draw_button(b, "New Game", x, y3, 100, bh, false);

    /* Separator line */
    SDL_SetRenderDrawColor(b->sdl_renderer, 80, 80, 90, 255);
    SDL_RenderDrawLine(b->sdl_renderer, 0, MENU_HEIGHT - 1, b->width, MENU_HEIGHT - 1);
}

static void sdl2_draw_thinking(Renderer *self, bool is_thinking) {
    if (!is_thinking) return;
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    if (b->font) {
        render_text(b, "AI thinking...", b->width - 80, MENU_HEIGHT / 2,
                    CLR_THINKING, b->font_small, true);
    }
}

/* ------------------------------------------------------------------ */
/*  Input polling                                                      */
/* ------------------------------------------------------------------ */

static InputEvent sdl2_poll_event(Renderer *self) {
    SDL2Backend *b = (SDL2Backend *)self->backend_data;
    InputEvent ev = { INPUT_NONE, 0, 0, 0, 0 };
    SDL_Event sdl_ev;

    while (SDL_PollEvent(&sdl_ev)) {
        switch (sdl_ev.type) {
        case SDL_QUIT:
            ev.type = INPUT_QUIT;
            return ev;

        case SDL_MOUSEBUTTONDOWN:
            if (sdl_ev.button.button == SDL_BUTTON_LEFT) {
                int mx = sdl_ev.button.x;
                int my = sdl_ev.button.y;

                /* Check menu buttons */
                SDL_Point pt = { mx, my };
                if (SDL_PointInRect(&pt, &btn_ucb1)) {
                    ev.type = INPUT_MENU_POLICY; ev.value = POLICY_UCB1; return ev;
                }
                if (SDL_PointInRect(&pt, &btn_puct)) {
                    ev.type = INPUT_MENU_POLICY; ev.value = POLICY_PUCT; return ev;
                }
                if (SDL_PointInRect(&pt, &btn_t02)) {
                    ev.type = INPUT_MENU_TIME; ev.value = 0; return ev;
                }
                if (SDL_PointInRect(&pt, &btn_t1)) {
                    ev.type = INPUT_MENU_TIME; ev.value = 1; return ev;
                }
                if (SDL_PointInRect(&pt, &btn_t3)) {
                    ev.type = INPUT_MENU_TIME; ev.value = 2; return ev;
                }
                if (SDL_PointInRect(&pt, &btn_side)) {
                    ev.type = INPUT_MENU_SIDE; return ev;
                }
                if (SDL_PointInRect(&pt, &btn_newgame)) {
                    ev.type = INPUT_MENU_NEWGAME; return ev;
                }

                /* Check board click */
                if (my >= MENU_HEIGHT && my < MENU_HEIGHT + BOARD_PX &&
                    mx >= 0 && mx < BOARD_PX) {
                    int col = mx / CELL_PX;
                    int display_row = (my - MENU_HEIGHT) / CELL_PX;
                    int row = 7 - display_row; /* flip to internal row */

                    ev.type = INPUT_CLICK;
                    ev.board_row = row;
                    ev.board_col = col;
                    return ev;
                }
            }
            break;

        case SDL_KEYDOWN:
            ev.type = INPUT_KEY;
            ev.key  = sdl_ev.key.keysym.sym;
            return ev;
        }
    }

    return ev;
}

static void sdl2_delay(Renderer *self, int ms) {
    (void)self;
    SDL_Delay(ms);
}

/* ------------------------------------------------------------------ */
/*  Factory                                                            */
/* ------------------------------------------------------------------ */

static Renderer sdl2_renderer_instance;

Renderer *renderer_sdl2_create(void) {
    Renderer *r = &sdl2_renderer_instance;
    memset(r, 0, sizeof(Renderer));

    r->init          = sdl2_init;
    r->destroy       = sdl2_destroy;
    r->begin_frame   = sdl2_begin_frame;
    r->end_frame     = sdl2_end_frame;
    r->draw_board    = sdl2_draw_board;
    r->draw_piece    = sdl2_draw_piece;
    r->draw_highlight= sdl2_draw_highlight;
    r->draw_status   = sdl2_draw_status;
    r->draw_menu     = sdl2_draw_menu;
    r->draw_thinking = sdl2_draw_thinking;
    r->poll_event    = sdl2_poll_event;
    r->delay         = sdl2_delay;
    r->backend_data  = NULL;

    return r;
}
