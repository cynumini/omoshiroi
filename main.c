#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>
#include <sqlite3.h>

#define DEBUG
#define SAKANA_IMPLEMENTATION
#include "sakana.h"

typedef struct {
    char *string;
    size_t len;
} StringView;

StringView create_string_view(char *string)
{
    return (StringView) { .string = string, .len = strlen(string) };
}

void strip(StringView *sv)
{
    while (sv->string[0] == ' ') {
        sv->string++;
        sv->len--;
        if (sv->len == 0) break;
    }
    while (sv->string[sv->len - 1] == ' ') {
        sv->len--;
        if (sv->len == 0) break;
    }
}

void usage(int argc, char *argv[])
{
    (void)argc;
    printf("usage: %s <command>\n\n", argv[0]);
    printf("Available omoshiroi commands:\n");
    printf("  add\t- Add media to the database\n");
    printf("  elo\t- Play elo matches between media\n");
    printf("  log\t- Log media\n");
    printf("  stats\t- Show stats\n");
}

int add_or_get_base(sqlite3 *db, char const *name, char const *table_name)
{
    sqlite3_stmt *stmt = NULL;
    char sql[256] = {0};
    sprintf(sql, "SELECT id FROM %s WHERE name = ?", table_name);
    assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
    assert(sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC) == SQLITE_OK);
    int result;
    int id = -1;
    do {
        result = sqlite3_step(stmt);
        if (result == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
        assert(result == SQLITE_ROW || result == SQLITE_DONE);
    } while (result != SQLITE_DONE);
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    if (id == -1) {
        sprintf(sql, "INSERT INTO %s (name) VALUES (?) RETURNING id", table_name);
        assert(sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) == SQLITE_OK);
        assert(sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC) == SQLITE_OK);
        do {
            result = sqlite3_step(stmt);
            if (result == SQLITE_ROW) id = sqlite3_column_int(stmt, 0);
            assert(result == SQLITE_ROW || result == SQLITE_DONE);
        } while (result != SQLITE_DONE);
        assert(sqlite3_finalize(stmt) == SQLITE_OK);
        return id;
    }
    return id;
}

int add_or_get_series(sqlite3 *db, char const *name)
{
    return add_or_get_base(db, name, "series");
}

int add_or_get_type(sqlite3 *db, char const *name)
{
    return add_or_get_base(db, name, "type");
}

int add_or_get_creator(sqlite3 *db, char const *name)
{
    return add_or_get_base(db, name, "creator");
}

define_da(char *, Options);

Options options;

static char *series_generator(const char *text, int state)
{
    // it's work but I not sure how
    static size_t len, i;
    char *name = NULL;

    if (!state) {
        i = 0;
        len = strlen(text);
    }

    for (; i < options.len; i) {
        char *name = options.items[i++];
        if (strncmp(name, text, len) == 0) {
            return strdup(name);
        }
    }

    return ((char *)NULL);
}

static char **series_completion(const char *text, int start, int end)
{
    (void)start;
    (void)end;
    rl_attempted_completion_over = 1;
    rl_completion_append_character = '\0';
    rl_completer_word_break_characters = "";
    return rl_completion_matches(text, series_generator);
}

// TODO: migrate from 0 to 1
// TODO: create tables if not exist

int main(int argc, char *argv[])
{
    Arena arena = alloc_arena(MB(1));
    sqlite3 *db = NULL;
    assert(sqlite3_open("elo.db", &db) == SQLITE_OK);

    da_append(&arena, &options, "ABC");
    da_append(&arena, &options, "CBA");
    da_append(&arena, &options, "ABCABC");
    
    int id = add_or_get_series(db, "hentai2");
    printf("id: %d\n", id);

    sqlite3_close(db);

    if (argc == 1) {
        usage(argc, argv);
    } else if (argc > 1) {
        if (strcmp(argv[1], "add") == 0) {
            printf("add\n");
            // rl_bind_key('\t', rl_insert);
            rl_bind_key('\t', rl_complete);
            rl_attempted_completion_function = series_completion;
            char *name     = readline("name: "); // text
            char *series   = readline("series: "); // key
            char *kind     = readline("type: "); // key
            char *release  = readline("release: "); // text
            char *japanese = readline("japanese (y/n): "); // int
            char *creator  = readline("creator: "); // key
            char *source   = readline("source: "); // key
            printf("name: %s\n", name);
            printf("series: %s\n", series);
            printf("kind: %s\n", kind);
            printf("release: %s\n", release);
            printf("japanese: %s\n", japanese);
            printf("creator: %s\n", creator);
            printf("source: %s\n", source);
            free(name);
            free(series);
            free(kind);
            free(release);
            free(japanese);
            free(creator);
            free(source);
        } else if (strcmp(argv[1], "elo") == 0) {
            printf("elo\n");
        } else if (strcmp(argv[1], "log") == 0) {
            printf("log\n");
        } else if (strcmp(argv[1], "stats") == 0) {
            printf("stats\n");
        } else {
            usage(argc, argv);
        }
    }

    delete_arena(arena);
    return 0;
}
