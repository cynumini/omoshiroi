#include <assert.h>
#include <sqlite3.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <readline/readline.h>
#include <sys/stat.h>

// #define DEBUG
#define SAKANA_IMPLEMENTATION
#include "sakana.h"

#define UNUSED(value) (void) value
define_da(char *, Strings);

typedef struct DatabaseManager {
    const char *file;
    const char *sql_path;
    sqlite3 *db;
} DatabaseManager;

bool is_file(const char *file)
{
    struct stat buf;
    if (stat(file, &buf) != 0)
        return false;
    return S_ISREG(buf.st_mode);
}

DatabaseManager create_database_manager()
{
    DatabaseManager self = { .file = "elo.db", .sql_path = "elo.sql" };
    bool exist = is_file(self.file);
    assert(sqlite3_open(self.file, &self.db) == SQLITE_OK);
    if (!exist) {
        const char *sql = "CREATE TABLE \"media\" (\n"
                          "    \"id\"       INTEGER NOT NULL UNIQUE,\n"
                          "    \"name\"     TEXT NOT NULL UNIQUE,\n"
                          "    \"type\"     TEXT NOT NULL,\n"
                          "    \"release\"  TEXT NOT NULL,\n"
                          "    \"elo\"      INT NOT NULL DEFAULT 1000,\n"
                          "    \"japanese\" INT NOT NULL,\n"
                          "    \"note\"     TEXT,\n"
                          "    PRIMARY KEY(\"id\" AUTOINCREMENT)\n"
                          ") STRICT;\n"
                          "CREATE TABLE \"log\" (\n"
                          "    \"id\"               INTEGER NOT NULL UNIQUE,\n"
                          "    \"timestamp\"        REAL NOT NULL UNIQUE,\n"
                          "    \"kind\"             TEXT NOT NULL,\n"
                          "    \"duration_seconds\" INTEGER NOT NULL,\n"
                          "    \"parent_id\"        INTEGER,\n"
                          "    \"characters\"       INTEGER,\n"
                          "    \"page\"             INTEGER,\n"
                          "    \"eps\"              REAL,\n"
                          "    \"yt_id\"            TEXT,\n"
                          "    \"yt_title\"         TEXT,\n"
                          "    \"notes\"            TEXT,\n"
                          "    PRIMARY KEY(\"id\" AUTOINCREMENT),\n"
                          "    FOREIGN KEY(\"parent_id\") REFERENCES \"media\"(\"id\")\n"
                          ") STRICT;\n";
        assert(sqlite3_exec(self.db, sql, NULL, NULL, NULL) == SQLITE_OK);
    }
    return self;
}

// WIP
void destroy_database_manager(DatabaseManager self)
{
    sqlite3_close(self.db);
}

char *arena_strdup(Arena *arena, const char *src)
{
    size_t n = strlen(src) + 1;
    char *dest = arena_alloc(arena, char, n);
    memcpy(dest, src, n);
    return dest;
}

Strings get_names(Arena *arena, DatabaseManager dm)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(dm.db, "SELECT \"name\" FROM \"media\"", -1, &stmt, NULL) == SQLITE_OK);
    Strings names = {};
    int step;
    while (true) {
        step = sqlite3_step(stmt);
        if (step == SQLITE_DONE)
            break;
        da_append(arena, &names, arena_strdup(arena, (const char *) sqlite3_column_text(stmt, 0)));
    }
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    return names;
}

Strings get_types(Arena *arena, DatabaseManager dm)
{
    sqlite3_stmt *stmt = NULL;
    assert(sqlite3_prepare_v2(dm.db, "SELECT DISTINCT \"type\" FROM \"media\"", -1, &stmt, NULL) == SQLITE_OK);
    Strings types = {};
    int step;
    while (true) {
        step = sqlite3_step(stmt);
        if (step == SQLITE_DONE)
            break;
        da_append(arena, &types, arena_strdup(arena, (const char *) sqlite3_column_text(stmt, 0)));
    }
    assert(sqlite3_finalize(stmt) == SQLITE_OK);
    return types;
}

// WIP
void add(DatabaseManager dm, const char *media_name, const char *media_type, const char *media_release,
         bool media_japanese, const char *media_note)
{
    UNUSED(dm);
    UNUSED(media_name);
    UNUSED(media_type);
    UNUSED(media_release);
    UNUSED(media_japanese);
    UNUSED(media_note);
}

Strings *compentry_func_strings;

char *compentry_func(const char *text, int state)
{
    static size_t i, n;
    const char *s;
    if (!state) {
        i = 0;
        n = strlen(text);
    }
    while (true) {
        s = compentry_func_strings->items[i++];
        if (i >= compentry_func_strings->len)
            break;
        if (strncmp(s, text, n) == 0)
            return strdup(s);
    }
    return (char *) NULL;
}

char **completion_function(const char *text, int start, int end)
{
    UNUSED(start);
    UNUSED(end);
    rl_attempted_completion_over = 1;
    rl_completion_append_character = '\0';
    rl_completer_word_break_characters = "";
    return rl_completion_matches(text, compentry_func);
}

// WIP
const char *prompt_readline(Arena *arena, const char *prompt, Strings options)
{
    UNUSED(options);
    size_t n = strlen(prompt) + strlen(": ") + 1;
    char *s = arena_alloc(arena, char *, n);
    sprintf(s, "%s: ", prompt);
    compentry_func_strings = &options;
    rl_attempted_completion_function = completion_function;
    const char *src = readline(s);
    const char *result = arena_strdup(arena, src);
    free((void *) src);
    return result;
}

// WIP
const char *prompt_date(const char *prompt)
{
    UNUSED(promppt);
    return "";
}

// WIP
bool prompt_check(const char *prompt)
{
    UNUSED(prompt);
    return true;
}

// WIP
const char *prompt(const char *prompt, bool option)
{
    UNUSED(prompt);
    UNUSED(option);
    return "";
}

int main()
{
    Arena arena = alloc_arena(MB(1));
    DatabaseManager dm = create_database_manager();
    // TODO: add args parsing instead of manual input
    const char *media_name = prompt_readline(&arena, "Name", get_names(&arena, dm));
    const char *media_type = prompt_readline(&arena, "Type", get_types(&arena, dm));
    const char *media_release = prompt_date("Release");
    bool media_japanese = prompt_check("Will I consume it in Japanese?");
    const char *media_note = prompt("Note", true);
    add(dm, media_name, media_type, media_release, media_japanese, media_note);
    destroy_database_manager(dm);
    delete_arena(arena);
    return 0;
}
