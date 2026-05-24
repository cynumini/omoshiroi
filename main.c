#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>
#include <sys/stat.h>

// #define DEBUG
#define SAKANA_IMPLEMENTATION
#include "sakana.h"

#define UNUSED(value) (void) value
define_da(char *, Strings);

typedef struct {
    char *string;
    size_t len;
} StringView;

StringView string_view_create(char *string)
{
    return (StringView) { .string = string, .len = strlen(string) };
}
void string_view_strip(StringView *sv)
{
    if (sv->len == 0)
        return;
    while (isspace(sv->string[0])) {
        sv->string++;
        sv->len--;
        if (sv->len == 0)
            break;
    }
    while (isspace(sv->string[sv->len - 1])) {
        sv->len--;
        if (sv->len == 0)
            break;
    }
}

static bool is_file(const char *file)
{
    struct stat buf;
    if (stat(file, &buf) != 0)
        return false;
    return S_ISREG(buf.st_mode);
}

int get_version(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_step(stmt) == SQLITE_ROW);
    int version = sqlite3_column_int(stmt, 0);
    assert(sqlite3_step(stmt) == SQLITE_DONE);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    return version;
}

static void migrate(sqlite3 *db)
{
    int version = get_version(db);
    if (version == 0) {
        printf("migrate from 0 to 1\n");
        const char *sql =
            "BEGIN;\n"
            "ALTER TABLE media RENAME TO media_old;\n"
            "CREATE TABLE media (\n"
            "    id       INTEGER NOT NULL UNIQUE,\n"
            "    name     TEXT NOT NULL,\n"
            "    kind     TEXT NOT NULL,\n"
            "    release  TEXT NOT NULL,\n"
            "    elo      INT NOT NULL DEFAULT 1000,\n"
            "    japanese INT NOT NULL,\n"
            "    state    INT NOT NULL DEFAULT 0,\n"
            "    note     TEXT,\n"
            "    PRIMARY KEY(id AUTOINCREMENT),\n"
            "    UNIQUE(name, kind)\n"
            ") STRICT;\n"
            "INSERT INTO media (id, name, kind, release, elo, japanese, note)\n"
            "SELECT id, name, type, release, elo, japanese, note\n"
            "FROM media_old;\n"
            "ALTER TABLE log RENAME TO log_old;\n"
            "CREATE TABLE log (\n"
            "    id               INTEGER NOT NULL UNIQUE,\n"
            "    timestamp        REAL NOT NULL UNIQUE,\n"
            "    kind             TEXT NOT NULL,\n"
            "    duration_seconds INTEGER NOT NULL,\n"
            "    parent_id        INTEGER,\n"
            "    characters       INTEGER,\n"
            "    page             INTEGER,\n"
            "    eps              REAL,\n"
            "    yt_id            TEXT,\n"
            "    yt_title         TEXT,\n"
            "    notes            TEXT,\n"
            "    PRIMARY KEY(id AUTOINCREMENT),\n"
            "    FOREIGN KEY(parent_id) REFERENCES media(id)\n"
            ") STRICT;\n"
            "INSERT INTO log (id, timestamp, kind, duration_seconds, parent_id, characters, page, eps, yt_id, "
            "yt_title, notes)\n"
            "SELECT id, timestamp, kind, duration_seconds, parent_id, characters, page, eps, yt_id, yt_title, notes\n"
            "FROM log_old;\n"
            "DROP TABLE log_old;\n"
            "DROP TABLE media_old;\n"
            "PRAGMA user_version = 1;\n"
            "COMMIT;";
        assert(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK);
    } else if (version == 1) {
        printf("migrate from 1 to 2\n");
        const char *sql = "BEGIN;\n"
                          "ALTER TABLE media\n"
                          "ADD COLUMN rank INTEGER NOT NULL DEFAULT 0;\n"
                          "PRAGMA user_version = 2;\n"
                          "COMMIT;";
        assert(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK);
    }
}

char *join_path(Arena *arena, const char *s1, const char *s2)
{
    size_t s1_len = strlen(s1);
    size_t len = s1_len + strlen(s2) + 1;
    bool have_slash = s1[s1_len - 1] == '/';
    if (!have_slash)
        len++;
    char *result = arena_alloc(arena, char, len);
    strcat(result, s1);
    if (!have_slash)
        strcat(result, "/");
    strcat(result, s2);
    return result;
}

static sqlite3 *open_database(const char *path)
{
    sqlite3 *db;
    bool exist = is_file(path);
    assert(sqlite3_open(path, &db) == SQLITE_OK);
    if (!exist) {
        const char *sql = "BEGIN;\n"
                          "CREATE TABLE media (\n"
                          "    id       INTEGER NOT NULL UNIQUE,\n"
                          "    name     TEXT NOT NULL UNIQUE,\n"
                          "    type     TEXT NOT NULL,\n"
                          "    release  TEXT NOT NULL,\n"
                          "    elo      INT NOT NULL DEFAULT 1000,\n"
                          "    japanese INT NOT NULL,\n"
                          "    note     TEXT,\n"
                          "    PRIMARY KEY(id AUTOINCREMENT)\n"
                          ") STRICT;\n"
                          "CREATE TABLE log (\n"
                          "    id               INTEGER NOT NULL UNIQUE,\n"
                          "    timestamp        REAL NOT NULL UNIQUE,\n"
                          "    kind             TEXT NOT NULL,\n"
                          "    duration_seconds INTEGER NOT NULL,\n"
                          "    parent_id        INTEGER,\n"
                          "    characters       INTEGER,\n"
                          "    page             INTEGER,\n"
                          "    eps              REAL,\n"
                          "    yt_id            TEXT,\n"
                          "    yt_title         TEXT,\n"
                          "    notes            TEXT,\n"
                          "    PRIMARY KEY(id AUTOINCREMENT),\n"
                          "    FOREIGN KEY(parent_id) REFERENCES media(id)\n"
                          ") STRICT;\n"
                          "PRAGMA user_version = 0;\n"
                          "COMMIT;";
        assert(sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK);
    }
    migrate(db);
    return db;
}

static char *arena_strdup(Arena *arena, const char *src)
{
    size_t n = strlen(src) + 1;
    char *dest = arena_alloc(arena, char, n);
    memcpy(dest, src, n);
    return dest;
}

char *arena_printf(Arena *arena, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    va_list copy;
    va_copy(copy, args);
    size_t len = (size_t) vsnprintf(NULL, 0, fmt, copy) + 1;
    va_end(copy);
    char *result = arena_alloc(arena, char, len);
    if (!result) {
        va_end(args);
        return NULL;
    }
    vsnprintf(result, len, fmt, args);
    va_end(args);
    return result;
}

static void string_view_bind(sqlite3_stmt *stmt, int index, StringView sv, bool null_on_zero_len)
{
    if (null_on_zero_len && sv.len == 0) {
        assert(sqlite3_bind_text(stmt, index, NULL, -1, SQLITE_STATIC) == SQLITE_OK);
        return;
    }
    assert(sqlite3_bind_text(stmt, index, sv.string, (int) sv.len, SQLITE_STATIC) == SQLITE_OK);
}

static void add(sqlite3 *db, StringView name, StringView kind, StringView release, bool japanese, int state,
                StringView note)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO media (name, kind, release, japanese, state, note) "
                      "VALUES (?, ?, ?, ?, ?, ?)";
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    string_view_bind(stmt, 1, name, false);
    string_view_bind(stmt, 2, kind, false);
    string_view_bind(stmt, 3, release, false);
    assert(sqlite3_bind_int(stmt, 4, japanese) == SQLITE_OK);
    assert(sqlite3_bind_int(stmt, 5, state) == SQLITE_OK);
    string_view_bind(stmt, 6, note, true);
    int result;
    bool is_print_busy_needed = true;
    do {
        result = sqlite3_step(stmt);
        assert(result == SQLITE_DONE || result == SQLITE_BUSY);
        if (result == SQLITE_BUSY && is_print_busy_needed) {
            printf("Database is busy!\n");
            is_print_busy_needed = false;
        }
    } while (result != SQLITE_DONE);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
}

static char *arena_readline(Arena *arena, const char *prompt)
{
    Save save = quicksave(arena);
    size_t n = strlen(prompt) + strlen(": ") + 1;
    char *fancy_prompt = arena_alloc(arena, char *, n);
    strcat(fancy_prompt, prompt);
    strcat(fancy_prompt, ": ");
    char *tmp = readline(fancy_prompt);
    quickload(arena, save);
    char *result = arena_strdup(arena, tmp);
    free(tmp);
    return result;
}

static Arena arena;

static sqlite3_stmt *sql_entry_func_stmt = NULL;

static char *sql_entry_func(const char *text, int index)
{
    if (index == 0) {
        assert(sqlite3_reset(sql_entry_func_stmt) == SQLITE_OK);
        Save save = quicksave(&arena);
        size_t len = strlen(text) + 1;
        char *parameter = arena_alloc(&arena, char, len + 1);
        strcat(parameter, text);
        strcat(parameter, "%");
        assert(sqlite3_bind_text(sql_entry_func_stmt, 1, parameter, (int) len, SQLITE_TRANSIENT) == SQLITE_OK);
        quickload(&arena, save);
    }
    int result = sqlite3_step(sql_entry_func_stmt);
    if (result == SQLITE_ROW) {
        char *value = (char *) sqlite3_column_text(sql_entry_func_stmt, 0);
        return strdup(value);
    } else if (result == SQLITE_DONE) {
        return NULL;
    }
    assert(false);
}

static char **sql_completion_function(const char *text, int start, int end)
{
    UNUSED(start);
    UNUSED(end);
    rl_bind_key('\t', rl_complete);
    rl_attempted_completion_over = 1;
    rl_completion_append_character = '\0';
    rl_completer_word_break_characters = "";
    return rl_completion_matches(text, sql_entry_func);
}

static StringView prompt_sql(const char *prompt, sqlite3 *db, const char *sql)
{
    assert(sqlite3_prepare_v2(db, sql, -1, &sql_entry_func_stmt, NULL) == SQLITE_OK);
    rl_attempted_completion_function = sql_completion_function;
    char *string = arena_readline(&arena, prompt);
    assert(sqlite3_finalize(sql_entry_func_stmt) == SQLITE_OK);
    StringView sv = string_view_create(string);
    string_view_strip(&sv);
    return sv;
}

unsigned int month_to_int(const char *m)
{
    if (!strcmp(m, "Jan") || !strcmp(m, "January"))
        return 1;
    if (!strcmp(m, "Feb") || !strcmp(m, "February"))
        return 2;
    if (!strcmp(m, "Mar") || !strcmp(m, "March"))
        return 3;
    if (!strcmp(m, "Apr") || !strcmp(m, "April"))
        return 4;
    if (!strcmp(m, "May"))
        return 5;
    if (!strcmp(m, "Jun") || !strcmp(m, "June"))
        return 6;
    if (!strcmp(m, "Jul") || !strcmp(m, "July"))
        return 7;
    if (!strcmp(m, "Aug") || !strcmp(m, "August"))
        return 8;
    if (!strcmp(m, "Sep") || !strcmp(m, "September"))
        return 9;
    if (!strcmp(m, "Oct") || !strcmp(m, "October"))
        return 10;
    if (!strcmp(m, "Nov") || !strcmp(m, "November"))
        return 11;
    if (!strcmp(m, "Dec") || !strcmp(m, "December"))
        return 12;
    return 0;
}

static StringView prompt_date(const char *prompt)
{
    rl_bind_key('\t', rl_insert);
    char *result = arena_alloc(&arena, char, strlen("0000-00-00") + 1);
    bool fail;
    do {
        Save save = quicksave(&arena);
        const char *src = arena_readline(&arena, prompt);
        char buffer[16] = {};
        unsigned int y, m, d;
        char rest;
        fail = !(sscanf(src, "%15s %u, %u%c", buffer, &d, &y, &rest) == 3);
        if (!fail) {
            m = month_to_int(buffer);
            fail = m == 0;
        }
        if (fail) {
            fail = !(sscanf(src, "%u年%u月%u日%c", &y, &m, &d, &rest) == 3);
        }
        if (fail) {
            fail = !(sscanf(src, "%u-%u-%u%c", &y, &m, &d, &rest) == 3);
        }
        quickload(&arena, save);
        if (!fail && (m > 12 || d > 31 || y > 9999)) {
            fail = true;
        }
        if (!fail) {
            sprintf(result, "%04u-%02u-%02u", y, m, d);
        }
    } while (fail);
    return string_view_create(result);
}

static bool prompt_check(const char *prompt)
{
    rl_bind_key('\t', rl_insert);
    while (true) {
        Save save = quicksave(&arena);
        const char *src = arena_readline(&arena, prompt);
        if (strlen(src) == 0) {
            quickload(&arena, save);
            continue;
        }
        char r = src[0];
        quickload(&arena, save);
        if (r == 'y' || r == 'Y') {
            return true;
        } else if (r == 'n' || r == 'N') {
            return false;
        }
    }
}

static StringView prompt(const char *prompt, bool empty_ok)
{
    rl_bind_key('\t', rl_insert);
    while (true) {
        const char *s = arena_readline(&arena, prompt);
        size_t len = strlen(s);
        if (len || empty_ok) {
            return string_view_create(arena_strdup(&arena, s));
        }
    }
}

static void usage(const char *name)
{
    printf("usage: %s <command>\n\n", name);
    printf("Available omoshiroi commands:\n");
    printf("  add   - Add media to the database\n");
    printf("  elo   - Play elo matches between media\n");
    printf("  log   - Log media\n");
    printf("  stats - Show stats\n");
}

typedef struct Player {
    int id;
    int elo;
    char *name;
    char *release;
    char *kind;
    char *note;
    char *str;
} Player;

define_da(Player, Players);

#define FIELDS " id, elo, release, name, kind, note "

#define RESET "\x1b[0m"
#define BLACK(NAME) "\x1b[30m" NAME RESET
#define RED(NAME) "\x1b[31m" NAME RESET
#define GREEN(NAME) "\x1b[32m" NAME RESET
#define YELLOW(NAME) "\x1b[33m" NAME RESET
#define BLUE(NAME) "\x1b[34m" NAME RESET
#define MAGENTA(NAME) "\x1b[35m" NAME RESET
#define CYAN(NAME) "\x1b[36m" NAME RESET
#define WHITE(NAME) "\x1b[37m" NAME RESET

static Player player_from_stmt(sqlite3_stmt *stmt)
{
    Player p = {};
    p.id = sqlite3_column_int(stmt, 0);
    p.elo = sqlite3_column_int(stmt, 1);
    p.release = arena_strdup(&arena, (char *) sqlite3_column_text(stmt, 2));
    p.name = arena_strdup(&arena, (char *) sqlite3_column_text(stmt, 3));
    p.kind = arena_strdup(&arena, (char *) sqlite3_column_text(stmt, 4));
    char *note = (char *) sqlite3_column_text(stmt, 5);
    if (note != NULL) {
        p.str = arena_printf(&arena, GREEN("%.4s") MAGENTA(" %s ") "(%s) (%s) (" GREEN("%d") ")", p.release, p.name,
                             p.kind, arena_strdup(&arena, note), p.elo);
    }
    p.str =
        arena_printf(&arena, GREEN("%.4s") MAGENTA(" %s ") "(%s) (" GREEN("%d") ")", p.release, p.name, p.kind, p.elo);

    return p;
}

static Players find_p1s(sqlite3 *db, size_t rank, size_t count)
{
    Players p1s = {};
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db,
                       "SELECT" FIELDS "FROM media WHERE rank = ? "
                       "ORDER BY RANDOM() LIMIT ?;",
                       -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, (int) rank);
    sqlite3_bind_int(stmt, 2, (int) count);
    int result, i = 0;
    do {
        result = sqlite3_step(stmt);
        assert(result == SQLITE_ROW || result == SQLITE_DONE);
        if (result == SQLITE_ROW) {
            da_append(&arena, &p1s, player_from_stmt(stmt));
            i++;
        }
    } while (result != SQLITE_DONE);
    sqlite3_finalize(stmt);
    assert(p1s.len <= count);
    return p1s;
}

static bool find_oponent_old(sqlite3 *db, Player p, size_t rank, const char *sql, Player *dest)
{
    bool result = false;
    sqlite3_stmt *stmt;
    sqlite3_prepare_v2(db, sql, -1, &stmt, NULL);
    sqlite3_bind_int(stmt, 1, (int) rank);
    sqlite3_bind_int(stmt, 2, p.elo);
    sqlite3_bind_int(stmt, 3, p.id);
    int step_result = sqlite3_step(stmt);
    assert(step_result == SQLITE_ROW || step_result == SQLITE_DONE);
    if (step_result == SQLITE_ROW) {
        *dest = player_from_stmt(stmt);
        assert(sqlite3_step(stmt) == SQLITE_DONE);
        result = true;
    }
    sqlite3_finalize(stmt);
    return result;
}

static Player find_oponent(sqlite3 *db, Player p, size_t rank)
{
    Player candidates[2] = {};
    size_t candidates_index = 0;
    if (find_oponent_old(db, p, rank,
                         "SELECT" FIELDS "FROM media WHERE rank = ? AND elo <= ? AND id != ? "
                         "ORDER BY elo DESC LIMIT 1;",
                         &candidates[candidates_index])) {
        candidates_index++;
    }
    if (find_oponent_old(db, p, rank,
                         "SELECT" FIELDS "FROM media WHERE rank = ? AND elo > ? "
                         "ORDER BY elo ASC LIMIT 1;",
                         &candidates[candidates_index])) {
        candidates_index++;
    }
    assert(candidates_index == 1 || candidates_index == 2);
    size_t choice = (size_t) rand() % candidates_index;
    return candidates[choice];
}

typedef enum {
    RESULT_NONE,
    RESULT_WIN,
    RESULT_LOSE,
    RESULT_DRAW,
} Result;

typedef struct Action {
    Player *p1;
    Player *p2;
    Result result;
} Action;

define_da(Action, Actions);

Action actions_pop(Actions *self, size_t index)
{
    assert(self != NULL);
    assert(index < self->len);
    Action removed = self->items[index];
    for (size_t i = index; i + 1 < self->len; i++) {
        self->items[i] = self->items[i + 1];
    }
    self->len--;
    return removed;
}

Action actions_pop_last(Actions *self)
{
    assert(self != NULL);
    assert(self->len > 0);
    self->len--;
    return self->items[self->len];
}

void actions_insert(Actions *self, size_t index, Action value)
{
    assert(self != NULL);
    assert(index <= self->len);
    assert(self->len < self->capacity);
    for (size_t i = self->len; i > index; i--) {
        self->items[i] = self->items[i - 1];
    }
    self->items[index] = value;
    self->len++;
}

typedef enum {
    RANK_UNRANKED,
    RANK_WOOD,
    RANK_STONE,
    RANK_IRON,
    RANK_GOLD,
    RANK_DIAMOND,
    RANK_WINNER,
    RANK_LEN,
} Rank;

#define UNREACHABLE()                                                 \
    do {                                                              \
        fprintf(stderr, "%s:%d:0 unreachable\n", __FILE__, __LINE__); \
        abort();                                                      \
    } while (0);

static char *rank_to_str(Rank rank)
{
    switch (rank) {
    case RANK_UNRANKED:
        return "Unranked";
    case RANK_WOOD:
        return "Wood";
    case RANK_STONE:
        return "Stone";
    case RANK_IRON:
        return "Iron";
    case RANK_GOLD:
        return "GOLD";
    case RANK_DIAMOND:
        return "Diamond";
    case RANK_WINNER:
        return "Winner";
    case RANK_LEN:
        UNREACHABLE();
    }
    UNREACHABLE();
}

#define MAX_GAMES_PER_RANK 100

static void elo_ranks(sqlite3 *db, size_t games_per_rank)
{
    for (size_t rank = RANK_UNRANKED; rank < RANK_WINNER; rank++) {
        printf(YELLOW("%s rank\n"), rank_to_str(rank));
        Save save = quicksave(&arena);
        Players players = find_p1s(db, rank, games_per_rank);
        Actions queue = { 0 };
        Actions logs = { 0 };
        size_t games_count = players.len;
        // find p2s
        for (size_t i = 0; i < games_count; i++) {
            Player *player1 = &players.items[i];
            da_append(&arena, &players, find_oponent(db, *player1, rank));
            Player *player2 = &players.items[players.len - 1];
            Action action = { .p1 = player1, .p2 = player2 };
            da_append(&arena, &queue, action);
        }
        do {
            if (queue.len == 0) {
                break;
            }
            Action action = actions_pop(&queue, 0);
            while (true) {
                printf("%s " RED("vs") " %s\n", action.p1->str, action.p2->str);
                char *input = arena_readline(&arena, "p1 win = 1, p2 win = 2, draw = 0, undo = u");
                if (strlen(input) == 0) {
                    continue;
                }
                Result result = RESULT_NONE;
                bool done = false;
                bool undo = false;
                switch (input[0]) {
                case '1': {
                    result = RESULT_WIN;
                    done = true;
                    break;
                }
                case '2': {
                    result = RESULT_LOSE;
                    done = true;
                    break;
                }
                case '0': {
                    result = RESULT_DRAW;
                    done = true;
                    break;
                }
                case 'u': {
                    if (logs.len > 0) {
                        actions_insert(&queue, 0, action);
                        actions_insert(&queue, 0, actions_pop_last(&logs));
                        undo = true;
                    }
                    break;
                }
                }
                if (done) {
                    action.result = result;
                    da_append(&arena, &logs, action);
                    break;
                }
                if (undo) {
                    break;
                }
            }
        } while (true);
        quickload(&arena, save);
    }
}

int main(int argc, char *argv[])
{
    arena = alloc_arena(KB(640));
    srand((unsigned int) time(NULL));

    const char *home_path = getenv("HOME");
    const char *database_path = join_path(&arena, home_path, "org/elo.db");
    sqlite3 *db = open_database(database_path);

    if (argc > 1) {
        if (strcmp(argv[1], "add") == 0) {
            StringView name = prompt_sql("Name", db, "SELECT name FROM media WHERE name LIKE ?");
            StringView kind = prompt_sql("Kind", db, "SELECT DISTINCT kind FROM media WHERE kind LIKE ?");
            StringView release = prompt_date("Release");
            bool japanese = prompt_check("Will I consume it in Japanese? (y/n)");

            int state = 0;
            bool completed = prompt_check("Completed? (y/n)");

            if (completed) {
                if (prompt_check("Want to re-experience? (y/n)")) {
                    state = 1;
                } else {
                    state = 2;
                }
            }

            StringView note = prompt("Note", true);

            add(db, name, kind, release, japanese, state, note);
        } else if (strcmp(argv[1], "elo") == 0) {
            if (argc < 3) {
                printf("usage: %s elo <command>\n\n", argv[0]);
                printf("Available omoshiroi elo subcommands:\n");
                printf("  ranks  - random matches in all ranks\n");
                printf("  promos - promotion matches between ranks\n");
                printf("  player - play as one player\n");
                printf("  season - run ranks + promos\n");
                printf("  show   - show ranked players and positions\n");
            } else if (strcmp(argv[2], "ranks") == 0) {
                if (argc != 4) {
                    printf("usage: %s elo ranks <games_per_rank>\n\n", argv[0]);
                    printf("Run random matches across all ranks (except winner)\n");
                    printf("  <games_per_rank> - required number of games per rank\n");
                    printf("Total matches = <games_per_rank> * 6 ranks\n");
                }
                if (argc == 4) {
                    int games_per_rank = atoi(argv[3]);
                    assert(games_per_rank > 0);
                    assert(games_per_rank <= MAX_GAMES_PER_RANK);
                    elo_ranks(db, (size_t) games_per_rank);
                }
            }

        } else {
            usage(argv[0]);
        }
    } else {
        usage(argv[0]);
    }

    sqlite3_close(db);
    delete_arena(arena);
    return 0;
}
