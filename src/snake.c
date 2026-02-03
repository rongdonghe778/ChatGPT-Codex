#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int x;
    int y;
} Point;

typedef struct {
    Point *cells;
    size_t length;
    size_t capacity;
} Snake;

typedef struct {
    int width;
    int height;
    Snake snake;
    Point food;
    int dir_x;
    int dir_y;
    int score;
    int game_over;
} Game;

static struct termios g_orig_termios;

static void restore_terminal(void) {
    tcsetattr(STDIN_FILENO, TCSANOW, &g_orig_termios);
}

static void handle_signal(int sig) {
    (void)sig;
    restore_terminal();
    write(STDOUT_FILENO, "\033[?25h\033[0m\n", 13);
    _exit(0);
}

static void setup_terminal(void) {
    struct termios raw;

    if (tcgetattr(STDIN_FILENO, &g_orig_termios) != 0) {
        perror("tcgetattr");
        exit(EXIT_FAILURE);
    }
    raw = g_orig_termios;
    raw.c_lflag &= (tcflag_t) ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        perror("tcsetattr");
        exit(EXIT_FAILURE);
    }

    atexit(restore_terminal);

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
}

static void snake_init(Snake *snake, size_t capacity) {
    snake->cells = calloc(capacity, sizeof(Point));
    if (!snake->cells) {
        perror("calloc");
        exit(EXIT_FAILURE);
    }
    snake->capacity = capacity;
    snake->length = 0;
}

static void snake_free(Snake *snake) {
    free(snake->cells);
    snake->cells = NULL;
    snake->length = 0;
    snake->capacity = 0;
}

static void snake_push_front(Snake *snake, Point p) {
    if (snake->length >= snake->capacity) {
        size_t new_capacity = snake->capacity * 2;
        Point *new_cells = realloc(snake->cells, new_capacity * sizeof(Point));
        if (!new_cells) {
            perror("realloc");
            exit(EXIT_FAILURE);
        }
        snake->cells = new_cells;
        snake->capacity = new_capacity;
    }
    memmove(&snake->cells[1], &snake->cells[0], snake->length * sizeof(Point));
    snake->cells[0] = p;
    snake->length += 1;
}

static void snake_pop_back(Snake *snake) {
    if (snake->length > 0) {
        snake->length -= 1;
    }
}

static int snake_contains(const Snake *snake, Point p) {
    for (size_t i = 0; i < snake->length; ++i) {
        if (snake->cells[i].x == p.x && snake->cells[i].y == p.y) {
            return 1;
        }
    }
    return 0;
}

static void game_place_food(Game *game) {
    Point p;
    do {
        p.x = (rand() % (game->width - 2)) + 1;
        p.y = (rand() % (game->height - 2)) + 1;
    } while (snake_contains(&game->snake, p));
    game->food = p;
}

static void game_init(Game *game, int width, int height) {
    game->width = width;
    game->height = height;
    game->dir_x = 1;
    game->dir_y = 0;
    game->score = 0;
    game->game_over = 0;
    snake_init(&game->snake, 16);

    Point start = { width / 2, height / 2 };
    snake_push_front(&game->snake, start);
    game_place_food(game);
}

static void game_reset(Game *game) {
    game->snake.length = 0;
    Point start = { game->width / 2, game->height / 2 };
    snake_push_front(&game->snake, start);
    game->dir_x = 1;
    game->dir_y = 0;
    game->score = 0;
    game->game_over = 0;
    game_place_food(game);
}

static void draw_border(int width, int height) {
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (y == 0 || y == height - 1 || x == 0 || x == width - 1) {
                putchar('#');
            } else {
                putchar(' ');
            }
        }
        putchar('\n');
    }
}

static void draw_game(const Game *game) {
    printf("\033[H");
    draw_border(game->width, game->height);

    printf("\033[%d;%dH@", game->food.y + 1, game->food.x + 1);

    for (size_t i = 0; i < game->snake.length; ++i) {
        const Point p = game->snake.cells[i];
        printf("\033[%d;%dH%c", p.y + 1, p.x + 1, i == 0 ? 'O' : 'o');
    }

    printf("\033[%d;1HScore: %d", game->height + 1, game->score);
    fflush(stdout);
}

static int read_input(int *dir_x, int *dir_y) {
    char buffer[3];
    fd_set set;
    struct timeval timeout = {0, 0};

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    int ready = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
    if (ready <= 0) {
        return 0;
    }

    ssize_t n = read(STDIN_FILENO, buffer, sizeof(buffer));
    if (n <= 0) {
        return 0;
    }

    if (buffer[0] == 'w' || buffer[0] == 'W') {
        *dir_x = 0;
        *dir_y = -1;
        return 1;
    }
    if (buffer[0] == 's' || buffer[0] == 'S') {
        *dir_x = 0;
        *dir_y = 1;
        return 1;
    }
    if (buffer[0] == 'a' || buffer[0] == 'A') {
        *dir_x = -1;
        *dir_y = 0;
        return 1;
    }
    if (buffer[0] == 'd' || buffer[0] == 'D') {
        *dir_x = 1;
        *dir_y = 0;
        return 1;
    }

    if (n >= 3 && buffer[0] == '\033' && buffer[1] == '[') {
        switch (buffer[2]) {
            case 'A':
                *dir_x = 0;
                *dir_y = -1;
                return 1;
            case 'B':
                *dir_x = 0;
                *dir_y = 1;
                return 1;
            case 'C':
                *dir_x = 1;
                *dir_y = 0;
                return 1;
            case 'D':
                *dir_x = -1;
                *dir_y = 0;
                return 1;
            default:
                break;
        }
    }

    return 0;
}

static void game_update(Game *game) {
    Point next = { game->snake.cells[0].x + game->dir_x,
                   game->snake.cells[0].y + game->dir_y };

    if (next.x <= 0 || next.x >= game->width - 1 || next.y <= 0 || next.y >= game->height - 1) {
        game->game_over = 1;
        return;
    }

    for (size_t i = 0; i < game->snake.length; ++i) {
        if (game->snake.cells[i].x == next.x && game->snake.cells[i].y == next.y) {
            game->game_over = 1;
            return;
        }
    }

    snake_push_front(&game->snake, next);

    if (next.x == game->food.x && next.y == game->food.y) {
        game->score += 10;
        game_place_food(game);
    } else {
        snake_pop_back(&game->snake);
    }
}

static void render_game_over(const Game *game) {
    printf("\033[%d;1HGame Over! Press R to restart or Q to quit.", game->height + 2);
    fflush(stdout);
}

static int read_game_over_choice(void) {
    char ch;
    fd_set set;
    struct timeval timeout = {0, 0};

    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);

    int ready = select(STDIN_FILENO + 1, &set, NULL, NULL, &timeout);
    if (ready <= 0) {
        return 0;
    }

    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n <= 0) {
        return 0;
    }

    if (ch == 'q' || ch == 'Q') {
        return -1;
    }
    if (ch == 'r' || ch == 'R') {
        return 1;
    }
    return 0;
}

int main(void) {
    Game game;
    srand((unsigned)time(NULL));

    setup_terminal();
    printf("\033[2J\033[?25l");

    game_init(&game, 40, 20);

    const int tick_us = 120000;

    while (1) {
        int new_dir_x = game.dir_x;
        int new_dir_y = game.dir_y;
        if (read_input(&new_dir_x, &new_dir_y)) {
            if (new_dir_x != -game.dir_x || new_dir_y != -game.dir_y) {
                game.dir_x = new_dir_x;
                game.dir_y = new_dir_y;
            }
        }

        if (!game.game_over) {
            game_update(&game);
            draw_game(&game);
        } else {
            render_game_over(&game);
            int choice = read_game_over_choice();
            if (choice == -1) {
                break;
            }
            if (choice == 1) {
                game_reset(&game);
                printf("\033[2J");
            }
        }

        usleep(tick_us);
    }

    printf("\033[?25h\033[0m\n");
    snake_free(&game.snake);
    return 0;
}
