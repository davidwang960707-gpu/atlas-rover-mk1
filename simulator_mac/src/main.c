#include <SDL.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum {
    PAGE_EYES = 0,
    PAGE_CLOCK,
    PAGE_STATUS,
    PAGE_VOICE,
    PAGE_SETTINGS,
} Page;

typedef enum {
    EXPR_IDLE = 0,
    EXPR_HAPPY,
    EXPR_LISTEN,
    EXPR_THINKING,
    EXPR_SPEAKING,
    EXPR_MOVING,
    EXPR_ERROR,
    EXPR_SLEEPY,
    EXPR_ANGRY,
    EXPR_SURPRISED,
} Expression;

typedef struct {
    Page page;
    Expression expr;
    int frame;
    bool running;
    int max_frames;
    const char *screenshot_path;
} AppState;

static void set_color(SDL_Renderer *r, int red, int green, int blue, int alpha)
{
    SDL_SetRenderDrawColor(r, (Uint8)red, (Uint8)green, (Uint8)blue, (Uint8)alpha);
}

static void fill_circle(SDL_Renderer *r, int cx, int cy, int radius)
{
    for (int y = -radius; y <= radius; ++y) {
        int dx = (int)sqrt((double)(radius * radius - y * y));
        SDL_RenderDrawLine(r, cx - dx, cy + y, cx + dx, cy + y);
    }
}

static void stroke_circle(SDL_Renderer *r, int cx, int cy, int radius)
{
    for (int a = 0; a < 360; ++a) {
        double rad = (double)a * M_PI / 180.0;
        int x = cx + (int)(cos(rad) * radius);
        int y = cy + (int)(sin(rad) * radius);
        SDL_RenderDrawPoint(r, x, y);
    }
}

static void thick_circle(SDL_Renderer *r, int cx, int cy, int radius, int width)
{
    for (int i = 0; i < width; ++i) {
        stroke_circle(r, cx, cy, radius - i);
    }
}

static void draw_arc(SDL_Renderer *r, int cx, int cy, int radius, int start_deg, int end_deg)
{
    for (int a = start_deg; a <= end_deg; ++a) {
        double rad = (double)a * M_PI / 180.0;
        int x = cx + (int)(cos(rad) * radius);
        int y = cy + (int)(sin(rad) * radius);
        SDL_RenderDrawPoint(r, x, y);
        SDL_RenderDrawPoint(r, x, y + 1);
        SDL_RenderDrawPoint(r, x, y - 1);
    }
}

static void draw_bar(SDL_Renderer *r, int x, int y, int w, int h, int value, int max)
{
    SDL_Rect border = {x, y, w, h};
    SDL_RenderDrawRect(r, &border);
    int fill_w = max > 0 ? (w - 4) * value / max : 0;
    SDL_Rect fill = {x + 2, y + 2, fill_w, h - 4};
    SDL_RenderFillRect(r, &fill);
}

static void draw_eye_screen(SDL_Renderer *r, int cx, int cy, Expression expr, int side, int frame)
{
    set_color(r, 11, 12, 14, 255);
    fill_circle(r, cx, cy, 112);
    set_color(r, 166, 108, 42, 255);
    thick_circle(r, cx, cy, 116, 5);
    set_color(r, 63, 201, 255, 255);

    int wobble = (int)(sin(frame / 14.0) * 5.0);
    int look = expr == EXPR_MOVING ? side * 10 : 0;

    switch (expr) {
    case EXPR_HAPPY:
        draw_arc(r, cx - 22 + look, cy + 5, 26, 200, 340);
        draw_arc(r, cx + 22 + look, cy + 5, 26, 200, 340);
        break;
    case EXPR_LISTEN:
        thick_circle(r, cx - 24 + wobble, cy, 13, 4);
        thick_circle(r, cx + 24 - wobble, cy, 13, 4);
        draw_arc(r, cx, cy + 25, 34, 210, 330);
        break;
    case EXPR_THINKING:
        draw_arc(r, cx - 25, cy - 3, 21, 245, 70);
        draw_arc(r, cx + 25, cy - 3, 21, 110, 295);
        set_color(r, 95, 225, 180, 255);
        SDL_RenderDrawLine(r, cx - 42, cy + 35, cx + 42, cy + 22);
        break;
    case EXPR_SPEAKING:
        thick_circle(r, cx - 28, cy - 4, 15 + abs(wobble), 3);
        thick_circle(r, cx + 28, cy - 4, 15 + abs(wobble), 3);
        draw_arc(r, cx, cy + 24, 28 + abs(wobble), 20, 160);
        break;
    case EXPR_MOVING:
        thick_circle(r, cx - 24 + look, cy - 2, 16, 4);
        thick_circle(r, cx + 24 + look, cy - 2, 16, 4);
        set_color(r, 95, 225, 180, 255);
        draw_arc(r, cx + look, cy + 28, 34, 210, 330);
        break;
    case EXPR_ERROR:
        set_color(r, 255, 82, 82, 255);
        SDL_RenderDrawLine(r, cx - 46, cy - 26, cx - 12, cy + 8);
        SDL_RenderDrawLine(r, cx - 12, cy - 26, cx - 46, cy + 8);
        SDL_RenderDrawLine(r, cx + 12, cy - 26, cx + 46, cy + 8);
        SDL_RenderDrawLine(r, cx + 46, cy - 26, cx + 12, cy + 8);
        draw_arc(r, cx, cy + 52, 30, 200, 340);
        break;
    case EXPR_SLEEPY:
        draw_arc(r, cx - 27, cy + 2, 24, 205, 335);
        draw_arc(r, cx + 27, cy + 2, 24, 205, 335);
        break;
    case EXPR_ANGRY:
        set_color(r, 255, 107, 75, 255);
        SDL_RenderDrawLine(r, cx - 50, cy - 30, cx - 8, cy - 8);
        SDL_RenderDrawLine(r, cx + 8, cy - 8, cx + 50, cy - 30);
        thick_circle(r, cx - 26, cy + 10, 12, 4);
        thick_circle(r, cx + 26, cy + 10, 12, 4);
        break;
    case EXPR_SURPRISED:
        thick_circle(r, cx - 28, cy, 20, 5);
        thick_circle(r, cx + 28, cy, 20, 5);
        set_color(r, 95, 225, 180, 255);
        fill_circle(r, cx, cy + 45, 8);
        break;
    case EXPR_IDLE:
    default:
        thick_circle(r, cx - 26 + look, cy + wobble / 2, 15, 4);
        thick_circle(r, cx + 26 + look, cy - wobble / 2, 15, 4);
        draw_arc(r, cx, cy + 28, 36, 210, 330);
        break;
    }
}

static void draw_clock_page(SDL_Renderer *r, int cx, int cy, int frame)
{
    (void)frame;
    time_t now = time(NULL);
    struct tm *tm_now = localtime(&now);
    int sec = tm_now ? tm_now->tm_sec : 0;
    int min = tm_now ? tm_now->tm_min : 0;
    int hour = tm_now ? tm_now->tm_hour % 12 : 0;

    set_color(r, 11, 12, 14, 255);
    fill_circle(r, cx, cy, 112);
    set_color(r, 166, 108, 42, 255);
    thick_circle(r, cx, cy, 116, 5);
    set_color(r, 63, 201, 255, 255);
    thick_circle(r, cx, cy, 76, 2);

    double sec_a = (sec * 6.0 - 90.0) * M_PI / 180.0;
    double min_a = (min * 6.0 - 90.0) * M_PI / 180.0;
    double hour_a = ((hour * 30.0) + min * 0.5 - 90.0) * M_PI / 180.0;

    SDL_RenderDrawLine(r, cx, cy, cx + (int)(cos(hour_a) * 36), cy + (int)(sin(hour_a) * 36));
    SDL_RenderDrawLine(r, cx, cy, cx + (int)(cos(min_a) * 58), cy + (int)(sin(min_a) * 58));
    set_color(r, 255, 107, 75, 255);
    SDL_RenderDrawLine(r, cx, cy, cx + (int)(cos(sec_a) * 66), cy + (int)(sin(sec_a) * 66));
    fill_circle(r, cx, cy, 5);
}

static void draw_status_page(SDL_Renderer *r)
{
    set_color(r, 25, 27, 31, 255);
    SDL_Rect panel = {110, 90, 540, 240};
    SDL_RenderFillRect(r, &panel);
    set_color(r, 166, 108, 42, 255);
    SDL_RenderDrawRect(r, &panel);

    set_color(r, 63, 201, 255, 255);
    draw_bar(r, 160, 130, 420, 28, 62, 100);
    draw_bar(r, 160, 180, 420, 28, 48, 100);
    draw_bar(r, 160, 230, 420, 28, 88, 100);
    draw_bar(r, 160, 280, 420, 28, 25, 100);
}

static void draw_voice_page(SDL_Renderer *r, int frame)
{
    set_color(r, 25, 27, 31, 255);
    SDL_Rect panel = {100, 85, 560, 250};
    SDL_RenderFillRect(r, &panel);
    set_color(r, 166, 108, 42, 255);
    SDL_RenderDrawRect(r, &panel);

    set_color(r, 63, 201, 255, 255);
    int base_y = 210;
    for (int x = 135; x < 625; x += 10) {
        int h = 20 + (int)(sin((x + frame * 5) / 25.0) * 50.0);
        SDL_RenderDrawLine(r, x, base_y - h, x, base_y + h);
    }
}

static void draw_settings_page(SDL_Renderer *r)
{
    int colors[][3] = {
        {63, 201, 255},
        {95, 225, 180},
        {255, 107, 75},
        {166, 108, 42},
        {245, 220, 150},
    };
    for (int i = 0; i < 5; ++i) {
        set_color(r, colors[i][0], colors[i][1], colors[i][2], 255);
        SDL_Rect swatch = {120 + i * 105, 145, 72, 72};
        SDL_RenderFillRect(r, &swatch);
        set_color(r, 235, 230, 220, 255);
        SDL_RenderDrawRect(r, &swatch);
    }
}

static void render(SDL_Renderer *r, const AppState *app)
{
    set_color(r, 7, 8, 10, 255);
    SDL_RenderClear(r);

    set_color(r, 166, 108, 42, 255);
    SDL_Rect frame = {42, 44, 676, 310};
    SDL_RenderDrawRect(r, &frame);

    if (app->page == PAGE_EYES) {
        draw_eye_screen(r, 250, 200, app->expr, -1, app->frame);
        draw_eye_screen(r, 510, 200, app->expr, 1, app->frame);
    } else if (app->page == PAGE_CLOCK) {
        draw_clock_page(r, 250, 200, app->frame);
        draw_clock_page(r, 510, 200, app->frame);
    } else if (app->page == PAGE_STATUS) {
        draw_status_page(r);
    } else if (app->page == PAGE_VOICE) {
        draw_voice_page(r, app->frame);
    } else if (app->page == PAGE_SETTINGS) {
        draw_settings_page(r);
    }

    set_color(r, 63, 201, 255, 255);
    SDL_Rect led = {235, 368, 290, 10};
    SDL_RenderFillRect(r, &led);
}

static void send_rover_command(const char *line)
{
    puts(line);
    fflush(stdout);
}

static void handle_key(AppState *app, SDL_Keycode key)
{
    switch (key) {
    case SDLK_ESCAPE:
    case SDLK_q:
        app->running = false;
        break;
    case SDLK_1:
        app->page = PAGE_EYES;
        break;
    case SDLK_2:
        app->page = PAGE_CLOCK;
        break;
    case SDLK_3:
        app->page = PAGE_STATUS;
        break;
    case SDLK_4:
        app->page = PAGE_VOICE;
        app->expr = EXPR_LISTEN;
        break;
    case SDLK_5:
        app->page = PAGE_SETTINGS;
        break;
    case SDLK_i:
        app->expr = EXPR_IDLE;
        app->page = PAGE_EYES;
        break;
    case SDLK_h:
        app->expr = EXPR_HAPPY;
        app->page = PAGE_EYES;
        break;
    case SDLK_l:
        app->expr = EXPR_LISTEN;
        app->page = PAGE_EYES;
        break;
    case SDLK_t:
        app->expr = EXPR_THINKING;
        app->page = PAGE_EYES;
        break;
    case SDLK_s:
        app->expr = EXPR_SPEAKING;
        app->page = PAGE_EYES;
        break;
    case SDLK_m:
        app->expr = EXPR_MOVING;
        app->page = PAGE_EYES;
        break;
    case SDLK_e:
        app->expr = EXPR_ERROR;
        app->page = PAGE_EYES;
        break;
    case SDLK_p:
        app->expr = EXPR_SLEEPY;
        app->page = PAGE_EYES;
        break;
    case SDLK_a:
        app->expr = EXPR_ANGRY;
        app->page = PAGE_EYES;
        break;
    case SDLK_u:
        app->expr = EXPR_SURPRISED;
        app->page = PAGE_EYES;
        break;
    case SDLK_SPACE:
        app->expr = EXPR_IDLE;
        app->page = PAGE_EYES;
        send_rover_command("AR1,STOP");
        break;
    case SDLK_UP:
        app->expr = EXPR_MOVING;
        app->page = PAGE_EYES;
        send_rover_command("AR1,MOVE,F,40,500");
        break;
    case SDLK_DOWN:
        app->expr = EXPR_MOVING;
        app->page = PAGE_EYES;
        send_rover_command("AR1,MOVE,B,35,400");
        break;
    case SDLK_LEFT:
        app->expr = EXPR_MOVING;
        app->page = PAGE_EYES;
        send_rover_command("AR1,TURN,L,30,350");
        break;
    case SDLK_RIGHT:
        app->expr = EXPR_MOVING;
        app->page = PAGE_EYES;
        send_rover_command("AR1,TURN,R,30,350");
        break;
    default:
        break;
    }
}

static bool save_bmp(SDL_Window *window, SDL_Renderer *renderer, const char *path)
{
    int w = 0;
    int h = 0;
    SDL_GetWindowSize(window, &w, &h);

    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 24, SDL_PIXELFORMAT_RGB24);
    if (!surface) {
        return false;
    }

    bool ok = SDL_RenderReadPixels(renderer, NULL, SDL_PIXELFORMAT_RGB24, surface->pixels, surface->pitch) == 0;
    if (ok) {
        ok = SDL_SaveBMP(surface, path) == 0;
    }
    SDL_FreeSurface(surface);
    return ok;
}

static void parse_args(AppState *app, int argc, char **argv)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            app->max_frames = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            app->screenshot_path = argv[++i];
        }
    }
}

int main(int argc, char **argv)
{
    AppState app = {
        .page = PAGE_EYES,
        .expr = EXPR_IDLE,
        .frame = 0,
        .running = true,
        .max_frames = 0,
        .screenshot_path = NULL,
    };
    parse_args(&app, argc, argv);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "Atlas Rover Mk.1 DualEye Simulator",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        760,
        420,
        SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    Uint32 renderer_flags = app.screenshot_path ? SDL_RENDERER_SOFTWARE : (SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, renderer_flags);
    if (!renderer) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    puts("Atlas DualEye simulator");
    puts("Keys: 1-5 pages, h/l/t/s/m/e expressions, arrows motion, space STOP, q quit");

    while (app.running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                app.running = false;
            } else if (event.type == SDL_KEYDOWN) {
                handle_key(&app, event.key.keysym.sym);
            }
        }

        render(renderer, &app);

        if (app.max_frames > 0 && app.frame + 1 >= app.max_frames) {
            if (app.screenshot_path) {
                if (!save_bmp(window, renderer, app.screenshot_path)) {
                    fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
                }
            }
            app.running = false;
        }

        SDL_RenderPresent(renderer);
        ++app.frame;
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
