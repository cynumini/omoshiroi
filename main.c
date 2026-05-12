#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <readline/readline.h>
#include <sqlite3.h>

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

// TODO: migrate from 0 to 1
// TODO: create tables if not exist

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    sqlite3 *db = NULL;
    assert(sqlite3_open("elo.db", &db) == SQLITE_OK);
    assert(db != NULL);
    sqlite3_close(db);

    if (argc == 1) {
        usage(argc, argv);
    } else if (argc > 1) {
        if (strcmp(argv[1], "add") == 0) {
            printf("add\n");
            rl_bind_key ('\t', rl_insert);
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

    return 0;
}
